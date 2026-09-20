/* SPDX-License-Identifier: Apache-2.0 */

#include <nuttx/config.h>
#include <nuttx/sdio.h>
#include <arch/chip/esp32p4_sdmmc.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <nuttx/mutex.h>
#include <nuttx/fs/fs.h>
#include <sys/mount.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>

#ifndef CONFIG_SDIO_MUXBUS
#  undef SDIO_LOCK
static int isolated_bus_lock(struct sdio_dev_s *dev, bool state)
{
  (void)dev;
  (void)state;
  return 0;
}
#  define SDIO_LOCK(dev, state) isolated_bus_lock(dev, state)
#endif

static mutex_t g_probe_lock = NXMUTEX_INITIALIZER;
static uint8_t g_sector[512] __attribute__((aligned(64)));
static uint8_t g_reference[512];
static struct sdio_dev_s *g_card;
static uint64_t g_sectors;
static bool g_registered;
static unsigned int g_write_rejections;
static uint32_t g_rca;
static bool g_allow_write;
static mutex_t g_io_lock = NXMUTEX_INITIALIZER;

/* Read-only whitelist: no write/erase commands. Slot ownership held until
 * reboot. The diagnostic block device rejects writes independently of VFS.
 */

static int command(struct sdio_dev_s *dev, uint32_t cmd, uint32_t arg,
                   uint32_t *response)
{
  int ret;
  switch (cmd)
    {
      case MMCSD_CMD0:
      case SD_CMD8:
      case SD_CMD55:
      case SD_ACMD41:
      case MMCSD_CMD2:
      case SD_CMD3:
      case MMCSD_CMD9:
      case MMCSD_CMD7S:
      case MMCSD_CMD17:
      case MMCSD_CMD13:
        break;
      case MMCSD_CMD24:
        if (!g_allow_write) return -EROFS;
        break;
      default:
        return -EPERM;
    }

  ret = SDIO_SENDCMD(dev, cmd, arg);
  if (ret >= 0) ret = SDIO_WAITRESPONSE(dev, cmd);
  if (ret < 0)
    {
      printf("SDRO: command %lu failed %d\n",
             (unsigned long)(cmd & MMCSD_CMDIDX_MASK), ret);
      return ret;
    }
  switch (cmd)
    {
      case MMCSD_CMD0: return 0;
      case SD_CMD8: return SDIO_RECVR7(dev, cmd, response);
      case SD_ACMD41: return SDIO_RECVR3(dev, cmd, response);
      case MMCSD_CMD2:
      case MMCSD_CMD9: return SDIO_RECVR2(dev, cmd, response);
      case SD_CMD3: return SDIO_RECVR6(dev, cmd, response);
      default: return SDIO_RECVR1(dev, cmd, response);
    }
}

static uint32_t le32(const uint8_t *p)
{
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
         (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int read_sector_locked(struct sdio_dev_s *dev, uint32_t lba)
{
  uint32_t r;
  sdio_eventset_t event;
  int ret;
  memset(g_sector, 0xa5, sizeof(g_sector));
  SDIO_BLOCKSETUP(dev, 512, 1);
  SDIO_WAITENABLE(dev, SDIOWAIT_TRANSFERDONE | SDIOWAIT_TIMEOUT |
                 SDIOWAIT_ERROR, 1000);
  ret = SDIO_DMARECVSETUP(dev, g_sector, sizeof(g_sector));
  if (ret >= 0) ret = command(dev, MMCSD_CMD17, lba, &r);
  if (ret >= 0 && (r & 0xfdffe088u)) ret = -EIO;
  if (ret < 0)
    {
      SDIO_CANCEL(dev);
      return ret;
    }
  event = SDIO_EVENTWAIT(dev);
  if ((event & (SDIOWAIT_ERROR | SDIOWAIT_TIMEOUT)) ||
      !(event & SDIOWAIT_TRANSFERDONE)) return -EIO;
  return 0;
}

static int read_sector(struct sdio_dev_s *dev, uint32_t lba)
{
  int ret = SDIO_LOCK(dev, true);
  int unlock;
  if (ret < 0) return ret;
  ret = read_sector_locked(dev, lba);
  unlock = SDIO_LOCK(dev, false);
  return ret < 0 ? ret : unlock;
}

static int probe(void)
{
  struct sdio_dev_s *dev;
  uint32_t r = 0;
  uint32_t cid[4];
  uint32_t csd[4];
  uint32_t rca;
  uint32_t csize;
  uint32_t start = 0;
  uint64_t sectors;
  int ret;
  int attempt;
  bool locked = false;

  if (g_registered) return -EBUSY;
  g_card = NULL;
  printf("SDRO: slot0, 400kHz, 1-bit, no mount/no writes\n");
  dev = esp32p4_sdmmc_sdio_initialize(0);
  if (!dev) return -EBUSY;
  ret = SDIO_LOCK(dev, true);
  if (ret < 0) return ret;
  locked = true;
  SDIO_CLOCK(dev, CLOCK_IDMODE);
  SDIO_WIDEBUS(dev, false);
  usleep(10000);

#define CHECK(c, a, p) do { ret = command(dev, c, a, p); \
                           if (ret < 0) goto done; } while (0)
  CHECK(MMCSD_CMD0, 0, &r);
  usleep(10000);
  CHECK(SD_CMD8, 0x1aa, &r);
  printf("SDRO: CMD8 echo=%08lx\n", (unsigned long)r);
  if (r != 0x1aa) { ret = -EPROTO; goto done; }
  for (attempt = 0; attempt < 100; attempt++)
    {
      CHECK(SD_CMD55, 0, &r);
      CHECK(SD_ACMD41, 0x40ff8000, &r);
      if (r & 0x80000000) break;
      usleep(10000);
    }
  printf("SDRO: OCR=%08lx attempts=%d\n", (unsigned long)r, attempt + 1);
  if (!(r & 0x80000000)) { ret = -ETIMEDOUT; goto done; }
  if (!(r & 0x40000000)) { ret = -ENOTSUP; goto done; }
  CHECK(MMCSD_CMD2, 0, cid);
  CHECK(SD_CMD3, 0, &r);
  rca = r & 0xffff0000;
  if (!rca) { ret = -EPROTO; goto done; }
  CHECK(MMCSD_CMD9, rca, csd);
  printf("SDRO: CID manufacturer=%02lx (serial withheld), RCA=%04lx\n",
         (unsigned long)(cid[0] >> 24), (unsigned long)(rca >> 16));
  printf("SDRO: CSD=%08lx %08lx %08lx %08lx\n",
         (unsigned long)csd[0], (unsigned long)csd[1],
         (unsigned long)csd[2], (unsigned long)csd[3]);
  if ((csd[0] >> 30) != 1) { ret = -ENOTSUP; goto done; }
  csize = ((csd[1] & 0x3f) << 16) | (csd[2] >> 16);
  sectors = (uint64_t)(csize + 1) * 1024;
  printf("SDRO: capacity_MiB=%lu sectors512=%llu\n",
         (unsigned long)((csize + 1) / 2),
         (unsigned long long)(csize + 1) * 1024);
  CHECK(MMCSD_CMD7S, rca, &r);
  if (r & 0xfdffe088u) { ret = -EIO; goto done; }
  usleep(10000);
  ret = SDIO_LOCK(dev, false);
  locked = false;
  if (ret < 0) goto done;
  ret = read_sector(dev, 0);
  if (ret < 0) goto done;
  if (g_sector[510] != 0x55 || g_sector[511] != 0xaa)
    { ret = -EBADMSG; goto done; }
  if (memcmp(g_sector + 3, "EXFAT   ", 8) != 0)
    {
      const uint8_t *p = g_sector + 446;
      uint32_t count = le32(p + 12);
      start = le32(p + 8);
      printf("SDRO: MBR partition1 type=%02x start=%lu sectors=%lu\n",
             p[4], (unsigned long)start, (unsigned long)count);
      if (p[4] != 7 || !start || !count ||
          (uint64_t)start + count > sectors)
        { ret = -ENOTSUP; goto done; }
      ret = read_sector(dev, start);
      if (ret < 0) goto done;
    }
  if (memcmp(g_sector + 3, "EXFAT   ", 8) != 0 ||
      g_sector[510] != 0x55 || g_sector[511] != 0xaa ||
      g_sector[108] != 9)
    { ret = -EBADMSG; goto done; }
  printf("SDRO: EXFAT boot signature PASS sector_shift=%u cluster_shift=%u\n",
         g_sector[108], g_sector[109]);
  memcpy(g_reference, g_sector, 512);
  for (attempt = 0; attempt < 100; attempt++)
    {
      ret = read_sector(dev, start);
      if (ret < 0) goto done;
      if (memcmp(g_reference, g_sector, 512) != 0)
        { ret = -EBADMSG; goto done; }
    }
  printf("SDRO: boot sector repeated reads 100/100 identical\n");
  g_card = dev;
  g_sectors = sectors;
  g_rca = rca;
  ret = 0;
done:
  if (locked) SDIO_LOCK(dev, false);
  printf("SDRO: identify %s ret=%d; filesystem untested\n",
         ret == 0 ? "PASS" : "FAIL", ret);
  return ret;
}

int microsd_readonly_identify(void)
{
  int ret = nxmutex_trylock(&g_probe_lock);
  if (ret < 0) return ret;
  ret = probe();
  if (ret < 0) printf("SDRO: command failed ret=%d\n", ret);
  nxmutex_unlock(&g_probe_lock);
  return ret;
}

static int block_open(struct inode *inode)
{
  return g_card ? 0 : -ENODEV;
}

static ssize_t block_read(struct inode *inode, unsigned char *buffer,
                          blkcnt_t start, unsigned int count)
{
  unsigned int i;
  int ret;
  if (!g_card || start < 0 || (uint64_t)start > g_sectors ||
      count > g_sectors - (uint64_t)start) return -EINVAL;
  ret = nxmutex_lock(&g_io_lock);
  if (ret < 0) return ret;
  for (i = 0; i < count; i++)
    {
      ret = read_sector(g_card, (uint32_t)(start + i));
      if (ret < 0) break;
      memcpy(buffer + (size_t)i * 512, g_sector, 512);
    }
  nxmutex_unlock(&g_io_lock);
  if (ret < 0) return ret;
  return count;
}

static ssize_t block_write(struct inode *inode, const unsigned char *buffer,
                           blkcnt_t start, unsigned int count)
{
  g_write_rejections++;
  return -EROFS;
}

static int block_geometry(struct inode *inode, struct geometry *geo)
{
  memset(geo, 0, sizeof(*geo));
  geo->geo_available = g_card != NULL;
  geo->geo_writeenabled = g_allow_write && inode->i_private != NULL;
  geo->geo_nsectors = g_sectors;
  geo->geo_sectorsize = 512;
  return 0;
}

static const struct block_operations g_readonly_ops =
{
  .open = block_open,
  .read = block_read,
  .write = block_write,
  .geometry = block_geometry
};

int microsd_mount_readonly(void)
{
  DIR *dir;
  int ret = nxmutex_trylock(&g_probe_lock);
  if (ret < 0) return ret;
  if (!g_registered)
    {
      ret = probe();
      if (ret < 0) goto done;
      ret = register_blockdriver("/dev/velafit-sdro", &g_readonly_ops,
                                 0444, NULL);
      if (ret < 0) goto done;
      g_registered = true;
    }
  /* NULL options: no autoformat/forceformat, even on invalid media. */
  g_write_rejections = 0;
  ret = mount("/dev/velafit-sdro", "/sdcard", "fatfs", MS_RDONLY, NULL);
  if (ret < 0) { ret = -errno; goto done; }
  dir = opendir("/sdcard");
  if (!dir) { ret = -errno; goto done; }
  ret = closedir(dir) < 0 ? -errno : 0;
done:
  printf("SDRO: exFAT mount/root-open %s ret=%d (lower writes=EROFS)\n",
         ret == 0 ? "PASS" : "FAIL", ret);
  printf("SDRO: filesystem write requests rejected=%u\n", g_write_rejections);
  nxmutex_unlock(&g_probe_lock);
  return ret;
}

/* This separate write path is reachable only during the explicit NEW-file
 * test, never through /dev/velafit-sdro. No erase/format commands exist.
 * TRM 56.4.3/56.5; use existing IDMAC send setup then CMD24, wait data done
 * and poll CMD13 READY_FOR_DATA / TRANSFER before reporting persistence.
 */
static int write_sector_locked(uint32_t lba, const uint8_t *data)
{
  uint32_t r;
  int ret;
  int attempt;
  sdio_eventset_t event;
  memcpy(g_sector, data, 512);
  SDIO_BLOCKSETUP(g_card, 512, 1);
  SDIO_WAITENABLE(g_card, SDIOWAIT_TRANSFERDONE | SDIOWAIT_TIMEOUT |
                 SDIOWAIT_ERROR, 1000);
  ret = SDIO_DMASENDSETUP(g_card, g_sector, 512);
  if (ret >= 0) ret = command(g_card, MMCSD_CMD24, lba, &r);
  if (ret >= 0 && (r & 0xfdffe088u)) ret = -EIO;
  if (ret < 0) { SDIO_CANCEL(g_card); return ret; }
  event = SDIO_EVENTWAIT(g_card);
  if ((event & (SDIOWAIT_ERROR | SDIOWAIT_TIMEOUT)) ||
      !(event & SDIOWAIT_TRANSFERDONE)) return -EIO;
  for (attempt = 0; attempt < 1000; attempt++)
    {
      ret = command(g_card, MMCSD_CMD13, g_rca, &r);
      if (ret < 0) return ret;
      if (r & 0xfdffe088u) return -EIO;
      if ((r & 0x1f00u) == 0x0900u) return 0;
      usleep(1000);
    }
  return -ETIMEDOUT;
}

static int write_sector(uint32_t lba, const uint8_t *data)
{
  int ret = SDIO_LOCK(g_card, true);
  int unlock;
  if (ret < 0) return ret;
  ret = write_sector_locked(lba, data);
  unlock = SDIO_LOCK(g_card, false);
  return ret < 0 ? ret : unlock;
}

static ssize_t block_write_test(struct inode *inode,
                                const unsigned char *buffer,
                                blkcnt_t start, unsigned int count)
{
  unsigned int i;
  int ret;
  if (!g_allow_write) return -EROFS;
  if (!g_card || start < 0 || (uint64_t)start > g_sectors ||
      count > g_sectors - (uint64_t)start) return -EINVAL;
  ret = nxmutex_lock(&g_io_lock);
  if (ret < 0) return ret;
  for (i = 0; i < count; i++)
    {
      ret = write_sector((uint32_t)(start + i), buffer + (size_t)i * 512);
      if (ret < 0) break;
    }
  nxmutex_unlock(&g_io_lock);
  if (ret < 0) return ret;
  return count;
}

static const struct block_operations g_test_ops =
{
  .open = block_open,
  .read = block_read,
  .write = block_write_test,
  .geometry = block_geometry
};

#define TEST_FILE "/sdcard/velafit-validation-20260915.bin"
#define TEST_BYTES 16384

static int check_test_file(void)
{
  uint8_t data[512];
  struct stat st;
  int fd;
  int ret = 0;
  size_t offset;
  size_t j;
  if (stat(TEST_FILE, &st) < 0) return -errno;
  if (st.st_size != TEST_BYTES) return -EBADMSG;
  fd = open(TEST_FILE, O_RDONLY);
  if (fd < 0) return -errno;
  for (offset = 0; offset < TEST_BYTES; offset += sizeof(data))
    {
      if (read(fd, data, sizeof(data)) != sizeof(data))
        { ret = -EIO; break; }
      for (j = 0; j < sizeof(data); j++)
        if (data[j] != (uint8_t)((offset + j) * 37 + 11))
          { ret = -EBADMSG; break; }
      if (ret < 0) break;
    }
  if (close(fd) < 0 && ret == 0) ret = -errno;
  return ret;
}

int microsd_file_test(bool readback)
{
  uint8_t data[512];
  int fd = -1;
  int ret;
  int endret;
  bool mounted = false;
  size_t offset;
  size_t j;
  if (readback)
    {
      ret = microsd_mount_readonly();
      if (ret < 0) return ret;
      ret = check_test_file();
      endret = umount("/sdcard");
      if (endret < 0 && ret == 0) ret = -errno;
      printf("SDTEST: reboot readback %s ret=%d bytes=%d\n",
             ret == 0 ? "PASS" : "FAIL", ret, TEST_BYTES);
      return ret;
    }

  ret = nxmutex_trylock(&g_probe_lock);
  if (ret < 0) return ret;
  if (g_registered) { ret = -EBUSY; goto done; }
  ret = probe();
  if (ret < 0) goto done;
  ret = register_blockdriver("/dev/velafit-sdtest", &g_test_ops, 0600,
                             (void *)1);
  if (ret < 0) goto done;
  g_registered = true;
  g_allow_write = true;
  if (mount("/dev/velafit-sdtest", "/sdcard", "fatfs", 0, NULL) < 0)
    { ret = -errno; goto done; }
  mounted = true;
  fd = open(TEST_FILE, O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (fd < 0) { ret = -errno; goto done; }
  for (offset = 0; offset < TEST_BYTES; offset += sizeof(data))
    {
      for (j = 0; j < sizeof(data); j++)
        data[j] = (uint8_t)((offset + j) * 37 + 11);
      if (write(fd, data, sizeof(data)) != sizeof(data))
        { ret = -EIO; goto done; }
    }
  if (fsync(fd) < 0) { ret = -errno; goto done; }
  ret = close(fd) < 0 ? -errno : 0;
  fd = -1;
  if (ret == 0) ret = check_test_file();
done:
  if (fd >= 0 && close(fd) < 0 && ret == 0) ret = -errno;
  if (mounted && umount("/sdcard") < 0 && ret == 0) ret = -errno;
  g_allow_write = false;
  printf("SDTEST: exclusive new file + fsync + verify + unmount %s ret=%d\n",
         ret == 0 ? "PASS" : "FAIL", ret);
  nxmutex_unlock(&g_probe_lock);
  return ret;
}

#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_345_VELAFIT_AI
#include "../velafit_ai/sync/velafit_outbox.h"

int microsd_outbox_ack(const char *id)
{
  int ret = microsd_mount_readonly();
  if (ret < 0) return ret;
  ret = vf_outbox_is_acked("/sdcard/velafit-outbox", id);
  int endret = umount("/sdcard");
  if (endret < 0) ret = -errno;
  printf("OUTBOX stored ACK id=%s result=%d (1=explicitACK,0=pending,negative=error) readonly=yes\n", id, ret);
  return ret == 1 ? 0 : ret < 0 ? ret : -EAGAIN;
}

/* The dedicated scheduler owns this mount and g_probe_lock until close.
 * A pre-existing unrelated directory is never adopted without its marker.
 */
int microsd_outbox_open(void)
{
  const char *dir = "/sdcard/velafit-outbox";
  const char *owner = "/sdcard/velafit-outbox/.owner";
  static const char magic[] = "VelaFit outbox format 1\n";
  char marker[sizeof(magic)];
  bool mounted = false;
  int ret = nxmutex_trylock(&g_probe_lock);
  int fd;
  if (ret < 0) return ret;
  if (!g_card)
    {
      ret = probe();
      if (ret < 0) goto failed;
    }
  ret = register_blockdriver("/dev/vf-outbox", &g_test_ops, 0600, (void *)1);
  if (ret < 0) goto failed;
  g_allow_write = true;
  if (mount("/dev/vf-outbox", "/sdcard", "fatfs", 0, NULL) < 0)
    {ret = -errno; goto unregister;}
  mounted = true;
  if (mkdir(dir, 0700) == 0)
    {
      fd = open(owner, O_WRONLY | O_CREAT | O_EXCL, 0600);
      if (fd < 0) {ret = -errno; goto unregister;}
      ret = write(fd, magic, sizeof(magic) - 1) == sizeof(magic) - 1 ? 0 : -EIO;
      if (ret == 0 && fsync(fd) < 0) ret = -errno;
      if (close(fd) < 0 && ret == 0) ret = -errno;
      if (ret < 0) goto unregister;
    }
  else
    {
      if (errno != EEXIST) {ret = -errno; goto unregister;}
      fd = open(owner, O_RDONLY);
      if (fd < 0) {ret = -EPERM; goto unregister;}
      ssize_t n = read(fd, marker, sizeof(marker));
      ret = n == sizeof(magic) - 1 && !memcmp(marker, magic, n) ? 0 : -EPERM;
      if (close(fd) < 0 && ret == 0) ret = -errno;
      if (ret < 0) goto unregister;
    }
  return 0;
unregister:
  if (mounted) umount("/sdcard");
  g_allow_write = false;
  unregister_blockdriver("/dev/vf-outbox");
failed:
  nxmutex_unlock(&g_probe_lock);
  return ret;
}

int microsd_outbox_close(void)
{
  int ret = umount("/sdcard") < 0 ? -errno : 0;
  g_allow_write = false;
  int endret = unregister_blockdriver("/dev/vf-outbox");
  nxmutex_unlock(&g_probe_lock);
  return ret ? ret : endret;
}

struct queue_test_sender { int calls; int result; };

struct cloud_queue_sender
{
  vf_outbox_sender sender;
  void *context;
  unsigned injected;
  unsigned real_calls;
  bool offline;
};

static int cloud_queue_send(void *arg, const char *summary)
{
  struct cloud_queue_sender *s = arg;
  if (s->offline)
    {
      s->injected++;
      return -ENETDOWN;
    }
  s->real_calls++;
  return s->sender ? s->sender(s->context, summary) : -EPERM;
}

/* New namespace only; explicit fault injection followed by the REAL sender.
 * Readback must find durable ACK and must never invoke a sender. This is not
 * physical link-loss nor production background scheduling acceptance.
 */
int microsd_cloud_queue_test(vf_outbox_sender sender, void *context,
                             bool readback)
{
  const char *dir = "/sdcard/vf-cloud-20260915";
  const char *id = "VF-SD-CLOUD-20260915";
  const char *summary = "{\"session_id\":\"VF-SD-CLOUD-20260915\","
    "\"exercise\":\"squat\",\"reps\":10,\"source\":\"fixed-test-summary\"}";
  struct cloud_queue_sender state = {sender, context, 0, 0, true};
  int ret;
  int endret;
  int64_t now = time(NULL);
  bool mounted = false;

  if (readback)
    {
      ret = microsd_mount_readonly();
      if (ret < 0) return ret;
      state.offline = false;
      ret = vf_outbox_is_acked(dir, id);
      if (ret == 1) ret = vf_outbox_tick(dir, now, cloud_queue_send, &state);
      else ret = ret < 0 ? ret : -EAGAIN;
      if (ret != 0 || state.real_calls) ret = -EIO;
      endret = umount("/sdcard");
      if (endret < 0 && ret == 0) ret = -errno;
      printf("SDCLOUD ACK replay %s ret=%d sender_calls=%u\n",
             ret == 0 ? "PASS" : "FAIL", ret, state.real_calls);
      return ret;
    }

  if (!sender || now < 1704067200) return -EINVAL;
  ret = nxmutex_trylock(&g_probe_lock);
  if (ret < 0) return ret;
  if (g_registered) {ret = -EBUSY; goto done;}
  ret = probe();
  if (ret < 0) goto done;
  ret = register_blockdriver("/dev/velafit-sdtest", &g_test_ops, 0600,
                             (void *)1);
  if (ret < 0) goto done;
  g_registered = true;
  g_allow_write = true;
  if (mount("/dev/velafit-sdtest", "/sdcard", "fatfs", 0, NULL) < 0)
    {ret = -errno; goto done;}
  mounted = true;
  if (mkdir(dir, 0700) < 0) {ret = -errno; goto done;}
  ret = vf_outbox_enqueue(dir, id, summary);
  if (ret < 0) goto done;
  ret = vf_outbox_tick(dir, now, cloud_queue_send, &state);
  if (ret != -ENETDOWN || state.injected != 1) {ret = -EIO; goto done;}
  ret = vf_outbox_tick(dir, now + 1, cloud_queue_send, &state);
  if (ret != 0 || state.injected != 1) {ret = -EIO; goto done;}
  state.offline = false;
  usleep(2100000);
  if (time(NULL) < now + 2) {ret = -EAGAIN; goto done;}
  ret = vf_outbox_tick(dir, time(NULL), cloud_queue_send, &state);
  if (ret != 1 || state.real_calls != 1) {if (ret >= 0) ret = -EIO; goto done;}
  ret = vf_outbox_enqueue(dir, id, summary);
  if (ret < 0) goto done;
  ret = vf_outbox_tick(dir, time(NULL), cloud_queue_send, &state);
  if (ret != 0 || state.real_calls != 1) ret = -EIO;
done:
  if (mounted && umount("/sdcard") < 0 && ret == 0) ret = -errno;
  g_allow_write = false;
  nxmutex_unlock(&g_probe_lock);
  printf("SDCLOUD persisted retry %s ret=%d injected_offline=%u real_sender_calls=%u\n",
         ret == 0 ? "PASS" : "FAIL", ret, state.injected, state.real_calls);
  return ret;
}
static int queue_test_send(void *arg, const char *summary)
{
  struct queue_test_sender *s=arg;
  if (!strstr(summary,"VF-SD-QUEUE-20260915")) return -EINVAL;
  s->calls++;
  return s->result;
}

/* Actual card persistence, explicitly simulated network sender. New private
 * validation namespace only. No format, delete, or overwrite operations. */
int microsd_queue_test(bool readback)
{
  const char *dir="/sdcard/vf-queue-test-20260915";
  struct queue_test_sender sender={0,-ENETDOWN};
  int ret, endret;
  bool mounted=false;
  if (readback)
    {
      ret=microsd_mount_readonly();
      if (ret<0) return ret;
      sender.result=0;
      ret=vf_outbox_is_acked(dir,"VF-SD-QUEUE-20260915");
      if (ret==1) ret=vf_outbox_tick(dir,200,queue_test_send,&sender);
      else ret=ret<0?ret:-EAGAIN;
      if (ret!=0 || sender.calls!=0) ret=-EIO;
      endret=umount("/sdcard");
      if (endret<0 && !ret) ret=-errno;
      printf("SDQUEUE reboot ACK replay %s ret=%d sender_calls=%d\n",
             !ret?"PASS":"FAIL",ret,sender.calls);
      return ret;
    }
  ret=nxmutex_trylock(&g_probe_lock);
  if (ret<0) return ret;
  if (g_registered) {ret=-EBUSY;goto done;}
  ret=probe();
  if (ret<0) goto done;
  ret=register_blockdriver("/dev/velafit-sdtest",&g_test_ops,0600,(void *)1);
  if (ret<0) goto done;
  g_registered=true;
  g_allow_write=true;
  if (mount("/dev/velafit-sdtest","/sdcard","fatfs",0,NULL)<0)
    {ret=-errno;goto done;}
  mounted=true;
  /* Exclusive directory: reruns never write into an existing directory. */
  if (mkdir(dir,0700)<0) {ret=-errno;goto done;}
  ret=vf_outbox_enqueue(dir,"VF-SD-QUEUE-20260915",
    "{\"session_id\":\"VF-SD-QUEUE-20260915\",\"exercise\":\"squat\",\"reps\":3}");
  if (ret) goto done;
  ret=vf_outbox_tick(dir,100,queue_test_send,&sender);
  if (ret!=-ENETDOWN || sender.calls!=1) {ret=-EIO;goto done;}
  ret=vf_outbox_tick(dir,101,queue_test_send,&sender);
  if (ret!=0 || sender.calls!=1) {ret=-EIO;goto done;}
  sender.result=0;
  ret=vf_outbox_tick(dir,102,queue_test_send,&sender);
  if (ret!=1 || sender.calls!=2) {ret=-EIO;goto done;}
  ret=vf_outbox_tick(dir,200,queue_test_send,&sender);
  if (ret!=0 || sender.calls!=2) ret=-EIO;
done:
  if (mounted && umount("/sdcard")<0 && !ret) ret=-errno;
  g_allow_write=false;
  nxmutex_unlock(&g_probe_lock);
  printf("SDQUEUE real exFAT + MOCK sender %s ret=%d calls=%d\n",
         !ret?"PASS":"FAIL",ret,sender.calls);
  return ret;
}
#endif
