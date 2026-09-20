/* SPDX-License-Identifier: Apache-2.0 */
#include "velafit_outbox.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <netutils/cJSON.h>

static int path_for(char *out, size_t cap, const char *dir, const char *id)
{
  if (!dir || !id || !id[0] || strlen(id)>31 ||
      strspn(id,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") != strlen(id))
    return -EINVAL;
  /* NAME_MAX=32 on the target. 64-bit hash keeps base + .stateNN <=29.
   * Hash collision is detected by comparing the immutable summary; it
   * returns EEXIST, never deduplicates two different session payloads. */
  uint64_t hash=UINT64_C(14695981039346656037);
  for (const unsigned char *p=(const unsigned char *)id;*p;p++)
    hash=(hash^*p)*UINT64_C(1099511628211);
  int n=snprintf(out,cap,"%s/%016llx.json",dir,(unsigned long long)hash);
  return n<0 || (size_t)n>=cap ? -ENAMETOOLONG : 0;
}

static cJSON *read_record(const char *path)
{
  char data[8192];
  FILE *f=fopen(path,"rb");
  if (!f) return NULL;
  size_t n=fread(data,1,sizeof(data)-1,f);
  int bad=ferror(f) || !feof(f);
  fclose(f);
  if (bad) { errno=EIO; return NULL; }
  data[n]=0;
  cJSON *value=cJSON_ParseWithOpts(data,NULL,1);
  if (!value) errno=EBADMSG;
  return value;
}

/* Immutable generation files avoid relying on replacement-rename semantics
 * absent in FatFs. Keep the original summary and at most ten attempt records.
 * A torn/corrupt generation is preserved and reported, never silently ACKed.
 */
static cJSON *load(const char *path)
{
  cJSON *record=read_record(path);
  if (!record) return NULL;
  for (unsigned generation=1;generation<=10;generation++)
    {
      char next[288];
      int n=snprintf(next,sizeof(next),"%s.state%02u",path,generation);
      if (n<0 || (size_t)n>=sizeof(next))
        { cJSON_Delete(record); errno=ENAMETOOLONG; return NULL; }
      cJSON *update=read_record(next);
      if (!update)
        {
          if (errno==ENOENT) break;
          cJSON_Delete(record); return NULL;
        }
      cJSON *old=cJSON_GetObjectItemCaseSensitive(record,"summary");
      cJSON *summary=cJSON_GetObjectItemCaseSensitive(update,"summary");
      cJSON *attempt=cJSON_GetObjectItemCaseSensitive(update,"attempts");
      if (!cJSON_IsString(old) || !cJSON_IsString(summary) ||
          strcmp(old->valuestring,summary->valuestring) ||
          !cJSON_IsNumber(attempt) || attempt->valuedouble!=generation)
        {
          cJSON_Delete(update); cJSON_Delete(record);
          errno=EBADMSG; return NULL;
        }
      cJSON_Delete(record); record=update;
    }
  return record;
}

static int save(const char *path, cJSON *record)
{
  char target[288];
  cJSON *attempts=cJSON_GetObjectItemCaseSensitive(record,"attempts");
  if (!cJSON_IsNumber(attempts) || attempts->valueint<0 ||
      attempts->valueint>10) return -EINVAL;
  int n=attempts->valueint ? snprintf(target,sizeof(target),"%s.state%02u",path,
                                    (unsigned)attempts->valueint) :
                            snprintf(target,sizeof(target),"%s",path);
  if(n<0 || (size_t)n>=sizeof(target)) return -ENAMETOOLONG;
  char *data=cJSON_PrintUnformatted(record);
  if(!data) return -ENOMEM;
  int fd=open(target,O_WRONLY|O_CREAT|O_EXCL,0600);
  int ret=0;
  if(fd<0) { cJSON_free(data); return -errno; }
  size_t length=strlen(data);
  size_t position=0;
  while(position<length)
    {
      ssize_t written=write(fd,data+position,length-position);
      if(written<0 && errno==EINTR) continue;
      if(written<=0) {ret=-EIO;break;}
      position+=(size_t)written;
    }
  if(!ret && fsync(fd)!=0) ret=-errno;
  if(close(fd)!=0 && !ret) ret=-errno;
#ifndef __NuttX__
  /* NuttX FatFs has no directory-fd fsync. f_sync above flushes file data
   * and entry metadata via CTRL_SYNC; power-cut behavior needs board tests. */
  if(!ret) {
    char parent[256];
    strcpy(parent,path);
    char *slash=strrchr(parent,'/');
    if(!slash) ret=-EINVAL;
    else {
      *slash=0;
      int fd=open(parent[0] ? parent : "/",O_RDONLY);
      if(fd<0) ret=-errno;
      else {if(fsync(fd)!=0) ret=-errno; close(fd);}
    }
  }
#endif
  cJSON_free(data);
  /* Target filesystem power-cut/metadata durability still needs validation. */
  return ret;
}

int vf_outbox_enqueue(const char *directory,const char *id,const char *summary)
{
  char path[256];
  int ret=path_for(path,sizeof(path),directory,id);
  if(ret || !summary || strlen(summary)>2048) return ret ? ret : -EINVAL;
  cJSON *payload=cJSON_ParseWithOpts(summary,NULL,1);
  cJSON *sid=cJSON_GetObjectItemCaseSensitive(payload,"session_id");
  int valid=cJSON_IsString(sid) && !strcmp(sid->valuestring,id);
  cJSON_Delete(payload);
  if(!valid) return -EINVAL;
  cJSON *record=load(path);
  if(record) {
    cJSON *old=cJSON_GetObjectItemCaseSensitive(record,"summary");
    ret=cJSON_IsString(old) && !strcmp(old->valuestring,summary) ? 0 : -EEXIST;
    cJSON_Delete(record);
    return ret;
  }
  if(errno!=ENOENT) return -errno;
  DIR *d=opendir(directory);
  if(!d) return -errno;
  unsigned count=0; struct dirent *entry;
  while((entry=readdir(d))) {
    size_t len=strlen(entry->d_name);
    if(len>5 && !strcmp(entry->d_name+len-5,".json")) ++count;
  }
  closedir(d);
  if(count>=32) return -ENOSPC;
  record=cJSON_CreateObject();
  if(!record) return -ENOMEM;
  if(!cJSON_AddStringToObject(record,"summary",summary) ||
     !cJSON_AddNumberToObject(record,"attempts",0) ||
     !cJSON_AddNumberToObject(record,"due",0) ||
     !cJSON_AddBoolToObject(record,"done",0)) ret=-ENOMEM;
  else ret=save(path,record);
  cJSON_Delete(record);
  return ret;
}

int vf_outbox_is_acked(const char *directory, const char *id)
{
  char path[256];
  int ret = path_for(path, sizeof(path), directory, id);
  if (ret < 0) return ret;
  cJSON *record = load(path);
  if (!record) return -errno;
  cJSON *done = cJSON_GetObjectItemCaseSensitive(record, "done");
  cJSON *attempt = cJSON_GetObjectItemCaseSensitive(record, "attempts");
  cJSON *summary = cJSON_GetObjectItemCaseSensitive(record, "summary");
  cJSON *payload = cJSON_IsString(summary) ?
                   cJSON_ParseWithOpts(summary->valuestring, NULL, 1) : NULL;
  cJSON *sid = cJSON_GetObjectItemCaseSensitive(payload, "session_id");
  ret = -EBADMSG;
  if (cJSON_IsBool(done) && cJSON_IsNumber(attempt) &&
      isfinite(attempt->valuedouble) && attempt->valuedouble >= 0 &&
      attempt->valuedouble <= 10 &&
      attempt->valuedouble == floor(attempt->valuedouble) &&
      cJSON_IsString(sid) && !strcmp(sid->valuestring, id))
    ret = cJSON_IsTrue(done) ? (attempt->valueint > 0 ? 1 : -EBADMSG) : 0;
  cJSON_Delete(payload);
  cJSON_Delete(record);
  return ret;
}

int vf_outbox_tick_report(const char *directory,int64_t now,
                         vf_outbox_sender sender,void *ctx,
                         struct vf_outbox_scan *scan)
{
  struct vf_outbox_scan local={0};
  if(!scan) scan=&local;
  memset(scan,0,sizeof(*scan));
  if(!directory || !sender || now<0 || now>4102444800LL) return -EINVAL;
  DIR *d=opendir(directory);
  if(!d) return -errno;
  struct dirent *entry; int ret=0;
  while((entry=readdir(d))) {
    size_t length=strlen(entry->d_name);
    if(length<6 || strcmp(entry->d_name+length-5,".json")) continue;
    ++scan->visited_records;
    char path[256];
    if(snprintf(path,sizeof(path),"%s/%s",directory,entry->d_name)>=(int)sizeof(path))
      {ret=-ENAMETOOLONG;break;}
    cJSON *record=load(path);
    if(!record)
      {
        ret=-errno;
        if (errno==EBADMSG) ++scan->corrupt_records;
        else ++scan->io_errors;
        continue;
      }
    cJSON *summary=cJSON_GetObjectItemCaseSensitive(record,"summary");
    cJSON *attempts=cJSON_GetObjectItemCaseSensitive(record,"attempts");
    cJSON *due=cJSON_GetObjectItemCaseSensitive(record,"due");
    cJSON *done=cJSON_GetObjectItemCaseSensitive(record,"done");
    if(!cJSON_IsString(summary) || strlen(summary->valuestring)>2048 ||
       !cJSON_IsNumber(attempts) || !isfinite(attempts->valuedouble) ||
       attempts->valuedouble != floor(attempts->valuedouble) ||
       attempts->valuedouble<0 || attempts->valuedouble>10 ||
       !cJSON_IsNumber(due) || !isfinite(due->valuedouble) ||
       due->valuedouble<0 || due->valuedouble>4102445100.0 ||
       !cJSON_IsBool(done))
      {cJSON_Delete(record);++scan->corrupt_records;ret=-EBADMSG;continue;}
    if(cJSON_IsTrue(done) ||
       cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(record,"permanent")) ||
       due->valuedouble>now || attempts->valueint>=10)
      {cJSON_Delete(record);continue;}
    int result=sender(ctx,summary->valuestring);
    unsigned attempt=(unsigned)attempts->valueint+1;
    bool permanent=result==-EACCES || result==-EINVAL || result==-EBADMSG;
    cJSON_SetNumberValue(attempts,attempt);
    unsigned delay=1u << (attempt>8 ? 8 : attempt);
    cJSON_SetNumberValue(due,(double)now+delay);
    done->type=result==0 ? cJSON_True : cJSON_False;
    if(permanent && !cJSON_AddBoolToObject(record,"permanent",1))
      {cJSON_Delete(record);ret=-ENOMEM;break;}
    ret=save(path,record);
    cJSON_Delete(record);
    if(!ret) ret=result==0 ? 1 : result;
    break;
  }
  closedir(d);
  return ret;
}

int vf_outbox_tick(const char *directory,int64_t now,vf_outbox_sender sender,void *ctx)
{
  return vf_outbox_tick_report(directory,now,sender,ctx,NULL);
}
