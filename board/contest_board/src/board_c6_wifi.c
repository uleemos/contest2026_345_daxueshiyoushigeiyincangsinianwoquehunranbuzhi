/****************************************************************************
 * board/contest_board/src/board_c6_wifi.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/arch.h>
#include <nuttx/kthread.h>
#include <nuttx/mutex.h>
#include <nuttx/net/netdev_lowerhalf.h>
#include <nuttx/sdio.h>
#include <nuttx/signal.h>
#include <nuttx/spinlock.h>

#include <arch/chip/gpio_sig_map.h>
#include <arch/chip/esp32p4_sdmmc.h>

#include "espressif/esp_gpio_p4.h"

#include "board_c6_wifi.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BOARD_C6_EN_GPIO          54
#define BOARD_C6_RESET_ASSERT_MS  50
#define BOARD_C6_BOOT_DELAY_MS    2500
#define BOARD_C6_SDIO_SLOT        1

#define SDIO_CCCR_REV             0x00
#define SDIO_CCCR_CISPTR0         0x09
#define SDIO_CCCR_CISPTR1         0x0a
#define SDIO_CCCR_CISPTR2         0x0b

#define CISTPL_END                0xff
#define CISTPL_MANFID             0x20
#define CIS_WALK_MAX_TUPLES       32

#define ESP_HOSTED_SDIO_FUNCTION  1
#define ESP_HOSTED_BLOCK_SIZE     512
#define ESP_HOSTED_ADDRESS_MASK   0x3ff
#define ESP_HOSTED_SCRATCH0_REG   0x3ff5506c
#define ESP_HOSTED_SCRATCH7_REG   0x3ff5508c
#define ESP_HOSTED_INT_RAW_REG    0x3ff55050
#define ESP_HOSTED_INT_CLR_REG    0x3ff550d4
#define ESP_HOSTED_PKT_LEN_REG    0x3ff55060
#define ESP_HOSTED_TOKEN_REG      0x3ff55044
#define ESP_HOSTED_FIFO_END       0x1f800
#define ESP_HOSTED_RX_BYTE_MASK   0xfffff
#define ESP_HOSTED_TX_BUF_MASK    0xfff
#define ESP_HOSTED_TX_BUF_MAX     0x1000
#define ESP_HOSTED_TX_BUF_SIZE    1536
#define ESP_HOSTED_NEW_PACKET     (1u << 23)
#define ESP_HOSTED_OPEN_DATA_PATH 0
#define ESP_HOSTED_RX_MAX         65536
#define ESP_HOSTED_RX_RETRIES     100

#define ESP_HOSTED_STA_IF         1
#define ESP_HOSTED_SERIAL_IF      3
#define ESP_HOSTED_PRIV_IF        5
#define ESP_HOSTED_PRIV_PKT_EVENT 0x33
#define ESP_HOSTED_PRIV_EVT_INIT  0x22

#define ESP_HOSTED_REQ_SET_MODE   260
#define ESP_HOSTED_REQ_GET_MAC    257
#define ESP_HOSTED_REQ_WIFI_INIT  278
#define ESP_HOSTED_REQ_WIFI_START 280
#define ESP_HOSTED_REQ_WIFI_CONN  282
#define ESP_HOSTED_REQ_WIFI_CFG   284
#define ESP_HOSTED_EVT_STA_CONN   775
#define ESP_HOSTED_EVT_STA_DISCON 776

#define ESP_HOSTED_WIFI_IF_STA    0
#define ESP_HOSTED_WIFI_MODE_STA  1
#define ESP_HOSTED_WIFI_MAGIC     0x1f2f3f4f
#define ESP_HOSTED_SSID_MAX       32
#define ESP_HOSTED_PASSWORD_MAX   64

#define C6_NET_MTU                1500
#define C6_NET_FRAME_MAX          (C6_NET_MTU + 14)
#define C6_NET_RX_QUOTA           16
#define C6_NET_RX_POLL_MS         10
#define C6_NET_RX_PRIORITY        100
#define C6_NET_RX_STACKSIZE       4096

begin_packed_struct struct esp_hosted_header_s
{
  uint8_t interface;
  uint8_t flags;
  uint16_t len;
  uint16_t offset;
  uint16_t checksum;
  uint16_t sequence;
  uint8_t throttle;
  uint8_t packet_type;
} end_packed_struct;

begin_packed_struct struct esp_hosted_priv_event_s
{
  uint8_t type;
  uint8_t len;
  uint8_t data[0];
} end_packed_struct;

struct board_c6_netdev_s
{
  struct netdev_lowerhalf_s lower;
  netpkt_queue_t rxqueue;
  spinlock_t lock;
  pid_t rxpid;
  bool registered;
  bool ifup;
  bool connected;
  bool running;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static FAR struct sdio_dev_s *g_c6_sdio;
static uint8_t aligned_data(64) g_c6_regbuf[ESP_HOSTED_BLOCK_SIZE];
static uint8_t aligned_data(64) g_c6_rxbuf[ESP_HOSTED_RX_MAX];
static uint8_t aligned_data(64) g_c6_txbuf[ESP_HOSTED_RX_MAX];
static uint8_t aligned_data(64) g_c6_net_txpayload[C6_NET_FRAME_MAX];
static uint32_t g_c6_rx_byte_count;
static size_t g_c6_rx_offset;
static size_t g_c6_rx_pending;
static uint32_t g_c6_tx_buf_count;
static uint32_t g_c6_rpc_uid;
static uint16_t g_c6_tx_sequence;
static bool g_c6_ready;
static mutex_t g_c6_transport_lock = NXMUTEX_INITIALIZER;
static struct board_c6_netdev_s g_c6_net;

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int board_c6_net_enqueue(FAR const uint8_t *payload, size_t len);
static void board_c6_net_set_connected(bool connected);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void board_c6_reset(void)
{
  /* A plain esp_configgpio(OUTPUT) only enables the pad driver.  Route the
   * GPIO output latch through the matrix as well; otherwise GPIO54 keeps its
   * previous matrix source and C6_EN does not receive a reliable pulse.
   */

  esp_gpiowrite(BOARD_C6_EN_GPIO, true);
  esp_gpio_matrix_out(BOARD_C6_EN_GPIO, SIG_GPIO_OUT_IDX, false, false);
  esp_configgpio(BOARD_C6_EN_GPIO, OUTPUT);
  up_mdelay(10);
  esp_gpiowrite(BOARD_C6_EN_GPIO, false);
  up_mdelay(BOARD_C6_RESET_ASSERT_MS);
  esp_gpiowrite(BOARD_C6_EN_GPIO, true);
  up_mdelay(BOARD_C6_BOOT_DELAY_MS);
}

static int board_c6_read_manfid(FAR struct sdio_dev_s *dev,
                                FAR uint16_t *vendor,
                                FAR uint16_t *device)
{
  uint32_t cisptr;
  uint8_t value;
  int ret;
  int i;

  ret = sdio_io_rw_direct(dev, false, 0, SDIO_CCCR_CISPTR0, 0, &value);
  if (ret < 0)
    {
      return ret;
    }

  cisptr = value;
  ret = sdio_io_rw_direct(dev, false, 0, SDIO_CCCR_CISPTR1, 0, &value);
  if (ret < 0)
    {
      return ret;
    }

  cisptr |= (uint32_t)value << 8;
  ret = sdio_io_rw_direct(dev, false, 0, SDIO_CCCR_CISPTR2, 0, &value);
  if (ret < 0)
    {
      return ret;
    }

  cisptr |= (uint32_t)value << 16;
  for (i = 0; i < CIS_WALK_MAX_TUPLES; i++)
    {
      uint8_t code;
      uint8_t link;
      uint8_t bytes[4];
      size_t j;

      ret = sdio_io_rw_direct(dev, false, 0, cisptr, 0, &code);
      if (ret < 0)
        {
          return ret;
        }

      if (code == CISTPL_END)
        {
          return -ENODATA;
        }

      ret = sdio_io_rw_direct(dev, false, 0, cisptr + 1, 0, &link);
      if (ret < 0)
        {
          return ret;
        }

      if (code == CISTPL_MANFID && link >= sizeof(bytes))
        {
          for (j = 0; j < sizeof(bytes); j++)
            {
              ret = sdio_io_rw_direct(dev, false, 0, cisptr + 2 + j, 0,
                                      &bytes[j]);
              if (ret < 0)
                {
                  return ret;
                }
            }

          *vendor = (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
          *device = (uint16_t)bytes[2] | ((uint16_t)bytes[3] << 8);
          return OK;
        }

      cisptr += 2 + link;
    }

  return -ENODATA;
}

static int board_c6_cmd53_dma_probe(FAR struct sdio_dev_s *dev,
                                    FAR uint32_t *scratch0)
{
  int ret;

  ret = sdio_enable_function(dev, ESP_HOSTED_SDIO_FUNCTION);
  if (ret < 0)
    {
      return ret;
    }

  ret = sdio_set_blocksize(dev, 0, ESP_HOSTED_BLOCK_SIZE);
  if (ret < 0)
    {
      return ret;
    }

  ret = sdio_set_blocksize(dev, ESP_HOSTED_SDIO_FUNCTION,
                           ESP_HOSTED_BLOCK_SIZE);
  if (ret < 0)
    {
      return ret;
    }

  /* ESP-Hosted accesses the SLC host registers through function 1.  A
   * multi-byte register access is CMD53 byte mode, and the P4 lower half
   * services it with its internal DMA.  Keep the buffer naturally aligned
   * because the controller DMA works on 32-bit words.
   */

  memset(g_c6_regbuf, 0xa5, sizeof(g_c6_regbuf));
  ret = sdio_io_rw_extended(dev, false, ESP_HOSTED_SDIO_FUNCTION,
                            ESP_HOSTED_SCRATCH0_REG &
                            ESP_HOSTED_ADDRESS_MASK,
                            true, g_c6_regbuf, sizeof(*scratch0), 0);
  if (ret < 0)
    {
      return ret;
    }

  memcpy(scratch0, g_c6_regbuf, sizeof(*scratch0));
  return OK;
}

static int board_c6_cmd53_reg(FAR struct sdio_dev_s *dev, bool write,
                              uint32_t address, FAR uint32_t *value)
{
  int ret;

  if (write)
    {
      memcpy(g_c6_regbuf, value, sizeof(*value));
    }
  else
    {
      memset(g_c6_regbuf, 0xa5, sizeof(g_c6_regbuf));
    }

  ret = sdio_io_rw_extended(dev, write, ESP_HOSTED_SDIO_FUNCTION,
                            address & ESP_HOSTED_ADDRESS_MASK, true,
                            g_c6_regbuf, sizeof(*value), 0);
  if (ret == OK && !write)
    {
      memcpy(value, g_c6_regbuf, sizeof(*value));
    }

  return ret;
}

static int board_c6_pb_put_varint(FAR uint8_t *buf, size_t size,
                                  FAR size_t *offset, uint64_t value)
{
  do
    {
      uint8_t byte = value & 0x7f;

      value >>= 7;
      if (value != 0)
        {
          byte |= 0x80;
        }

      if (*offset >= size)
        {
          return -ENOSPC;
        }

      buf[(*offset)++] = byte;
    }
  while (value != 0);

  return OK;
}

static int board_c6_pb_put_field(FAR uint8_t *buf, size_t size,
                                 FAR size_t *offset, uint32_t field,
                                 uint64_t value)
{
  int ret;

  ret = board_c6_pb_put_varint(buf, size, offset, (uint64_t)field << 3);
  if (ret < 0)
    {
      return ret;
    }

  return board_c6_pb_put_varint(buf, size, offset, value);
}

static int board_c6_pb_put_bytes(FAR uint8_t *buf, size_t size,
                                 FAR size_t *offset, uint32_t field,
                                 FAR const uint8_t *data, size_t len)
{
  int ret;

  ret = board_c6_pb_put_varint(buf, size, offset,
                               ((uint64_t)field << 3) | 2);
  if (ret < 0)
    {
      return ret;
    }

  ret = board_c6_pb_put_varint(buf, size, offset, len);
  if (ret < 0)
    {
      return ret;
    }

  if (len > size - *offset)
    {
      return -ENOSPC;
    }

  memcpy(buf + *offset, data, len);
  *offset += len;
  return OK;
}

static int board_c6_pb_get_varint(FAR const uint8_t *buf, size_t len,
                                  FAR size_t *offset,
                                  FAR uint64_t *value)
{
  uint64_t result = 0;
  unsigned int shift = 0;

  while (*offset < len && shift < 64)
    {
      uint8_t byte = buf[(*offset)++];

      result |= (uint64_t)(byte & 0x7f) << shift;
      if ((byte & 0x80) == 0)
        {
          *value = result;
          return OK;
        }

      shift += 7;
    }

  return -EPROTO;
}

static int board_c6_pb_find(FAR const uint8_t *buf, size_t len,
                            uint32_t wanted, FAR uint64_t *number,
                            FAR const uint8_t **bytes,
                            FAR size_t *bytes_len)
{
  size_t offset = 0;

  while (offset < len)
    {
      uint64_t key;
      uint64_t value;
      uint32_t field;
      uint8_t wire;
      int ret;

      ret = board_c6_pb_get_varint(buf, len, &offset, &key);
      if (ret < 0)
        {
          return ret;
        }

      field = key >> 3;
      wire = key & 7;
      if (wire == 0)
        {
          ret = board_c6_pb_get_varint(buf, len, &offset, &value);
          if (ret < 0)
            {
              return ret;
            }

          if (field == wanted)
            {
              if (number != NULL)
                {
                  *number = value;
                }

              return OK;
            }
        }
      else if (wire == 2)
        {
          ret = board_c6_pb_get_varint(buf, len, &offset, &value);
          if (ret < 0 || value > len - offset)
            {
              return -EPROTO;
            }

          if (field == wanted)
            {
              if (bytes != NULL)
                {
                  *bytes = buf + offset;
                }

              if (bytes_len != NULL)
                {
                  *bytes_len = value;
                }

              return OK;
            }

          offset += value;
        }
      else
        {
          return -EPROTO;
        }
    }

  return -ENOENT;
}

static int board_c6_hosted_send(FAR struct sdio_dev_s *dev,
                                uint16_t msgid, uint32_t uid,
                                FAR const uint8_t *request,
                                size_t request_len)
{
  FAR struct esp_hosted_header_s *header;
  uint8_t protobuf[256];
  uint32_t token;
  uint32_t available;
  size_t protobuf_len = 0;
  size_t payload_len;
  size_t frame_len;
  size_t nblocks;
  size_t needed;
  size_t offset;
  unsigned int retries;
  int ret;

  ret = board_c6_pb_put_field(protobuf, sizeof(protobuf), &protobuf_len,
                              1, 1);
  if (ret >= 0)
    {
      ret = board_c6_pb_put_field(protobuf, sizeof(protobuf), &protobuf_len,
                                  2, msgid);
    }

  if (ret >= 0)
    {
      ret = board_c6_pb_put_field(protobuf, sizeof(protobuf), &protobuf_len,
                                  3, uid);
    }

  if (ret >= 0)
    {
      ret = board_c6_pb_put_bytes(protobuf, sizeof(protobuf), &protobuf_len,
                                  msgid, request, request_len);
    }

  if (ret < 0)
    {
      return ret;
    }

  memset(g_c6_txbuf, 0, sizeof(g_c6_txbuf));
  header = (FAR struct esp_hosted_header_s *)g_c6_txbuf;
  offset = sizeof(*header);

  g_c6_txbuf[offset++] = 1;
  g_c6_txbuf[offset++] = 6;
  g_c6_txbuf[offset++] = 0;
  memcpy(g_c6_txbuf + offset, "RPCRsp", 6);
  offset += 6;
  g_c6_txbuf[offset++] = 2;
  g_c6_txbuf[offset++] = protobuf_len & 0xff;
  g_c6_txbuf[offset++] = protobuf_len >> 8;
  memcpy(g_c6_txbuf + offset, protobuf, protobuf_len);
  offset += protobuf_len;

  payload_len = offset - sizeof(*header);
  frame_len = offset;
  nblocks = (frame_len + ESP_HOSTED_BLOCK_SIZE - 1) /
            ESP_HOSTED_BLOCK_SIZE;
  needed = (frame_len + ESP_HOSTED_TX_BUF_SIZE - 1) /
           ESP_HOSTED_TX_BUF_SIZE;
  header->interface = ESP_HOSTED_SERIAL_IF;
  header->len = payload_len;
  header->offset = sizeof(*header);

  for (retries = 0; retries < 50; retries++)
    {
      ret = board_c6_cmd53_reg(dev, false, ESP_HOSTED_TOKEN_REG, &token);
      if (ret < 0)
        {
          return ret;
        }

      available = (((token >> 16) & ESP_HOSTED_TX_BUF_MASK) +
                   ESP_HOSTED_TX_BUF_MAX - g_c6_tx_buf_count) &
                  ESP_HOSTED_TX_BUF_MASK;
      if (available >= needed)
        {
          break;
        }

      up_mdelay(10);
    }

  if (retries == 50)
    {
      return -EBUSY;
    }

  ret = sdio_io_rw_extended(dev, true, ESP_HOSTED_SDIO_FUNCTION,
                            ESP_HOSTED_FIFO_END - frame_len,
                            true, g_c6_txbuf, ESP_HOSTED_BLOCK_SIZE,
                            nblocks);
  if (ret < 0)
    {
      return ret;
    }

  g_c6_tx_buf_count += needed;
  g_c6_tx_buf_count &= ESP_HOSTED_TX_BUF_MASK;
  return OK;
}

static int board_c6_hosted_send_data(FAR struct sdio_dev_s *dev,
                                     FAR const uint8_t *payload,
                                     size_t payload_len)
{
  FAR struct esp_hosted_header_s *header;
  uint32_t token;
  uint32_t available;
  size_t frame_len;
  size_t nblocks;
  size_t needed;
  unsigned int retries;
  int ret;

  if (payload == NULL || payload_len == 0 ||
      payload_len > C6_NET_FRAME_MAX ||
      payload_len > sizeof(g_c6_txbuf) - sizeof(*header))
    {
      return -EMSGSIZE;
    }

  frame_len = sizeof(*header) + payload_len;
  nblocks = (frame_len + ESP_HOSTED_BLOCK_SIZE - 1) /
            ESP_HOSTED_BLOCK_SIZE;
  needed = (frame_len + ESP_HOSTED_TX_BUF_SIZE - 1) /
           ESP_HOSTED_TX_BUF_SIZE;

  memset(g_c6_txbuf, 0, sizeof(g_c6_txbuf));
  header = (FAR struct esp_hosted_header_s *)g_c6_txbuf;
  header->interface = ESP_HOSTED_STA_IF;
  header->len = payload_len;
  header->offset = sizeof(*header);
  header->sequence = g_c6_tx_sequence++;
  memcpy(g_c6_txbuf + sizeof(*header), payload, payload_len);

  for (retries = 0; retries < 50; retries++)
    {
      ret = board_c6_cmd53_reg(dev, false, ESP_HOSTED_TOKEN_REG, &token);
      if (ret < 0)
        {
          return ret;
        }

      available = (((token >> 16) & ESP_HOSTED_TX_BUF_MASK) +
                   ESP_HOSTED_TX_BUF_MAX - g_c6_tx_buf_count) &
                  ESP_HOSTED_TX_BUF_MASK;
      if (available >= needed)
        {
          break;
        }

      up_mdelay(10);
    }

  if (retries == 50)
    {
      return -EBUSY;
    }

  ret = sdio_io_rw_extended(dev, true, ESP_HOSTED_SDIO_FUNCTION,
                            ESP_HOSTED_FIFO_END - frame_len,
                            true, g_c6_txbuf, ESP_HOSTED_BLOCK_SIZE,
                            nblocks);
  if (ret < 0)
    {
      return ret;
    }

  g_c6_tx_buf_count = (g_c6_tx_buf_count + needed) &
                      ESP_HOSTED_TX_BUF_MASK;
  return OK;
}

static int board_c6_hosted_receive(FAR struct sdio_dev_s *dev,
                                   FAR uint16_t *msgid,
                                   FAR uint32_t *uid,
                                   FAR const uint8_t **rpc_payload,
                                   FAR size_t *rpc_payload_len,
                                   unsigned int timeout_ms)
{
  FAR struct esp_hosted_header_s *header;
  FAR const uint8_t *protobuf;
  FAR const uint8_t *payload;
  FAR const uint8_t *nested;
  uint32_t interrupts;
  uint32_t packet_count;
  uint32_t packet_len;
  size_t nblocks;
  uint64_t value;
  size_t protobuf_len;
  size_t nested_len;
  unsigned int elapsed;
  int ret;

  for (elapsed = 0; elapsed < timeout_ms; elapsed += 10)
    {
      if (g_c6_rx_pending == 0)
        {
          ret = board_c6_cmd53_reg(dev, false, ESP_HOSTED_INT_RAW_REG,
                                   &interrupts);
          if (ret < 0)
            {
              return ret;
            }

          ret = board_c6_cmd53_reg(dev, false, ESP_HOSTED_PKT_LEN_REG,
                                   &packet_count);
          if (ret < 0)
            {
              return ret;
            }

          packet_count &= ESP_HOSTED_RX_BYTE_MASK;
          packet_len = (packet_count + ESP_HOSTED_RX_BYTE_MASK + 1 -
                        g_c6_rx_byte_count) & ESP_HOSTED_RX_BYTE_MASK;
          if (packet_len == 0)
            {
              up_mdelay(10);
              continue;
            }

          /* Use the cumulative packet byte counter as the source of truth.
           * INT_RAW is an edge notification and can be cleared by a closely
           * following response while the previous packet is acknowledged.
           * The byte counter cannot lose that state.  One counter delta can
           * contain several back-to-back ESP-Hosted frames, so retain the
           * complete delta and consume those frames one at a time below.
           */

          if (packet_len < sizeof(*header) ||
              packet_len > sizeof(g_c6_rxbuf))
            {
              syslog(LOG_ERR,
                     "ERROR: ESP32-C6 RX aggregate length=%lu max=%lu\n",
                     (unsigned long)packet_len,
                     (unsigned long)sizeof(g_c6_rxbuf));
              return -EMSGSIZE;
            }

          nblocks = (packet_len + ESP_HOSTED_BLOCK_SIZE - 1) /
                    ESP_HOSTED_BLOCK_SIZE;

          ret = sdio_io_rw_extended(dev, false, ESP_HOSTED_SDIO_FUNCTION,
                                    ESP_HOSTED_FIFO_END - packet_len,
                                    true, g_c6_rxbuf, ESP_HOSTED_BLOCK_SIZE,
                                    nblocks);
          if (ret < 0)
            {
              return ret;
            }

          g_c6_rx_byte_count = packet_count;
          g_c6_rx_offset = 0;
          g_c6_rx_pending = packet_len;
          ret = board_c6_cmd53_reg(dev, true, ESP_HOSTED_INT_CLR_REG,
                                   &interrupts);
          if (ret < 0)
            {
              return ret;
            }
        }

      header = (FAR struct esp_hosted_header_s *)
               (g_c6_rxbuf + g_c6_rx_offset);
      if (header->interface == 0 && header->len == 0 &&
          header->offset == 0)
        {
          /* Older ESP-Hosted SDIO firmware may include zero padding in the
           * cumulative receive count.  Padding is not another frame.
           */

          g_c6_rx_pending = 0;
          continue;
        }

      packet_len = header->offset + header->len;
      if (packet_len < sizeof(*header) || packet_len > g_c6_rx_pending)
        {
          g_c6_rx_pending = 0;
          return -EPROTO;
        }

      g_c6_rx_offset += packet_len;
      g_c6_rx_pending -= packet_len;
      if (header->offset < sizeof(*header))
        {
          g_c6_rx_pending = 0;
          return -EPROTO;
        }

      payload = (FAR const uint8_t *)header + header->offset;
      if ((header->interface & 0x0f) == ESP_HOSTED_STA_IF)
        {
          board_c6_net_enqueue(payload, header->len);
          *msgid = 0;
          *uid = 0;
          return OK;
        }

      if ((header->interface & 0x0f) != ESP_HOSTED_SERIAL_IF)
        {
          continue;
        }

      if (header->len < 15 || payload[0] != 1 || payload[1] != 6 ||
          payload[2] != 0 ||
          (memcmp(payload + 3, "RPCRsp", 6) != 0 &&
           memcmp(payload + 3, "RPCEvt", 6) != 0) ||
          payload[9] != 2)
        {
          continue;
        }

      protobuf_len = payload[10] | ((size_t)payload[11] << 8);
      if (protobuf_len > header->len - 12)
        {
          return -EPROTO;
        }

      protobuf = payload + 12;
      ret = board_c6_pb_find(protobuf, protobuf_len, 2, &value, NULL, NULL);
      if (ret < 0)
        {
          return ret;
        }

      *msgid = value;
      ret = board_c6_pb_find(protobuf, protobuf_len, 3, &value, NULL, NULL);
      *uid = ret == OK ? value : 0;

      ret = board_c6_pb_find(protobuf, protobuf_len, *msgid, NULL,
                             &nested, &nested_len);
      if (ret < 0)
        {
          return ret;
        }

      *rpc_payload = nested;
      *rpc_payload_len = nested_len;
      return OK;
    }

  return -ETIMEDOUT;
}

static int board_c6_rpc(FAR struct sdio_dev_s *dev, uint16_t request_id,
                        FAR const uint8_t *request, size_t request_len,
                        FAR const uint8_t **response,
                        FAR size_t *response_len)
{
  uint16_t msgid;
  uint32_t uid;
  uint32_t wanted_uid;
  unsigned int retries;
  int ret;

  ret = nxmutex_lock(&g_c6_transport_lock);
  if (ret < 0)
    {
      return ret;
    }

  wanted_uid = ++g_c6_rpc_uid;
  ret = board_c6_hosted_send(dev, request_id, wanted_uid,
                             request, request_len);
  if (ret < 0)
    {
      goto out;
    }

  for (retries = 0; retries < 8; retries++)
    {
      ret = board_c6_hosted_receive(dev, &msgid, &uid, response,
                                    response_len, 1000);
      if (ret == -ETIMEDOUT)
        {
          continue;
        }

      if (ret < 0)
        {
          goto out;
        }

      if (msgid == request_id + 256 && uid == wanted_uid)
        {
          ret = OK;
          goto out;
        }

      syslog(LOG_INFO, "ESP32-C6 RPC event: id=%u uid=%lu\n",
             msgid, (unsigned long)uid);
    }

  ret = -ETIMEDOUT;

out:
  nxmutex_unlock(&g_c6_transport_lock);
  return ret;
}

static int board_c6_rpc_status(FAR struct sdio_dev_s *dev,
                               uint16_t request_id,
                               FAR const uint8_t *request,
                               size_t request_len)
{
  FAR const uint8_t *response;
  size_t response_len;
  uint64_t status;
  int ret;

  ret = board_c6_rpc(dev, request_id, request, request_len,
                     &response, &response_len);
  if (ret < 0)
    {
      return ret;
    }

  ret = board_c6_pb_find(response, response_len, 1, &status, NULL, NULL);
  if (ret == -ENOENT)
    {
      /* proto3 omits scalar fields carrying their zero default. */

      return OK;
    }

  if (ret < 0)
    {
      return ret;
    }

  return (int32_t)status == 0 ? OK : -(int32_t)status;
}

static int board_c6_wifi_rpc_init(FAR struct sdio_dev_s *dev)
{
  uint8_t config[128];
  uint8_t request[144];
  size_t config_len = 0;
  size_t request_len = 0;
  int ret;

#define PUT_INIT_FIELD(f, v) \
  do \
    { \
      ret = board_c6_pb_put_field(config, sizeof(config), &config_len, \
                                  (f), (v)); \
      if (ret < 0) \
        { \
          return ret; \
        } \
    } \
  while (0)

  /* ESP-IDF 5.3 WIFI_INIT_CONFIG_DEFAULT values used by the C6 image. */

  PUT_INIT_FIELD(1, 10);                 /* static_rx_buf_num */
  PUT_INIT_FIELD(2, 32);                 /* dynamic_rx_buf_num */
  PUT_INIT_FIELD(3, 1);                  /* dynamic TX buffers */
  PUT_INIT_FIELD(4, 0);                  /* static_tx_buf_num */
  PUT_INIT_FIELD(5, 32);                 /* dynamic_tx_buf_num */
  PUT_INIT_FIELD(6, 0);                  /* cache_tx_buf_num */
  PUT_INIT_FIELD(7, 0);                  /* csi_enable */
  PUT_INIT_FIELD(8, 1);                  /* ampdu_rx_enable */
  PUT_INIT_FIELD(9, 1);                  /* ampdu_tx_enable */
  PUT_INIT_FIELD(10, 0);                 /* amsdu_tx_enable */
  PUT_INIT_FIELD(11, 1);                 /* nvs_enable */
  PUT_INIT_FIELD(12, 0);                 /* nano_enable */
  PUT_INIT_FIELD(13, 6);                 /* rx_ba_win */
  PUT_INIT_FIELD(14, 0);                 /* wifi_task_core_id */
  PUT_INIT_FIELD(15, 752);               /* beacon_max_len */
  PUT_INIT_FIELD(16, 32);                /* mgmt_sbuf_num */
  PUT_INIT_FIELD(17, 0);                 /* optional feature caps */
  PUT_INIT_FIELD(18, 1);                 /* sta_disconnected_pm */
  PUT_INIT_FIELD(19, 7);                 /* espnow_max_encrypt_num */
  PUT_INIT_FIELD(20, ESP_HOSTED_WIFI_MAGIC);

#undef PUT_INIT_FIELD

  ret = board_c6_pb_put_bytes(request, sizeof(request), &request_len, 1,
                              config, config_len);
  if (ret < 0)
    {
      return ret;
    }

  return board_c6_rpc_status(dev, ESP_HOSTED_REQ_WIFI_INIT,
                             request, request_len);
}

static int board_c6_wifi_rpc_set_config(FAR struct sdio_dev_s *dev,
                                        FAR const char *ssid,
                                        FAR const char *password)
{
  uint8_t station[160];
  uint8_t config[176];
  uint8_t request[192];
  uint8_t empty = 0;
  size_t station_len = 0;
  size_t config_len = 0;
  size_t request_len = 0;
  int ret;

  ret = board_c6_pb_put_bytes(station, sizeof(station), &station_len, 1,
                              (FAR const uint8_t *)ssid, strlen(ssid));
  if (ret >= 0)
    {
      ret = board_c6_pb_put_bytes(station, sizeof(station), &station_len, 2,
                                  (FAR const uint8_t *)password,
                                  strlen(password));
    }

  /* The C6 implementation expects threshold and PMF submessages even when
   * all their fields use defaults.
   */

  if (ret >= 0)
    {
      ret = board_c6_pb_put_bytes(station, sizeof(station), &station_len, 9,
                                  &empty, 0);
    }

  if (ret >= 0)
    {
      ret = board_c6_pb_put_bytes(station, sizeof(station), &station_len, 10,
                                  &empty, 0);
    }

  /* The C6 slave forces all-channel scan for STA mode.  Allow a few
   * association retries so a transiently missed phone-hotspot beacon does
   * not turn into an immediate user-visible failure.
   */

  if (ret >= 0)
    {
      ret = board_c6_pb_put_field(station, sizeof(station), &station_len, 13,
                                  3);
    }

  if (ret >= 0)
    {
      ret = board_c6_pb_put_bytes(config, sizeof(config), &config_len, 2,
                                  station, station_len);
    }

  if (ret >= 0)
    {
      ret = board_c6_pb_put_field(request, sizeof(request), &request_len, 1,
                                  ESP_HOSTED_WIFI_IF_STA);
    }

  if (ret >= 0)
    {
      ret = board_c6_pb_put_bytes(request, sizeof(request), &request_len, 2,
                                  config, config_len);
    }

  if (ret < 0)
    {
      return ret;
    }

  return board_c6_rpc_status(dev, ESP_HOSTED_REQ_WIFI_CFG,
                             request, request_len);
}

static int board_c6_wifi_wait_connected(FAR struct sdio_dev_s *dev)
{
  FAR const uint8_t *payload;
  FAR const uint8_t *event;
  size_t payload_len;
  size_t event_len;
  uint16_t msgid;
  uint32_t uid;
  uint64_t reason;
  unsigned int retries;
  int ret;

  for (retries = 0; retries < 30; retries++)
    {
      ret = board_c6_hosted_receive(dev, &msgid, &uid, &payload,
                                    &payload_len, 1000);
      if (ret == -ETIMEDOUT)
        {
          continue;
        }

      if (ret < 0)
        {
          return ret;
        }

      if (msgid == ESP_HOSTED_EVT_STA_CONN)
        {
          return OK;
        }

      if (msgid == ESP_HOSTED_EVT_STA_DISCON)
        {
          reason = 0;
          ret = board_c6_pb_find(payload, payload_len, 2, NULL,
                                 &event, &event_len);
          if (ret == OK)
            {
              ret = board_c6_pb_find(event, event_len, 4, &reason,
                                     NULL, NULL);
            }

          syslog(LOG_ERR,
                 "ERROR: ESP32-C6 Wi-Fi disconnected: reason=%lu\n",
                 (unsigned long)(ret == OK ? reason : 0));
          return -ENETUNREACH;
        }

      syslog(LOG_INFO, "ESP32-C6 Wi-Fi event: id=%u\n", msgid);
    }

  return -ETIMEDOUT;
}

static int board_c6_hosted_handshake(FAR struct sdio_dev_s *dev)
{
  FAR struct esp_hosted_header_s *header;
  FAR struct esp_hosted_priv_event_s *event;
  uint32_t interrupts;
  uint32_t packet_len;
  size_t nblocks;
  uint8_t open = 1u << ESP_HOSTED_OPEN_DATA_PATH;
  int retries;
  int ret;

  /* Tell the factory ESP-Hosted firmware that the host data path is ready. */

  ret = sdio_io_rw_direct(dev, true, ESP_HOSTED_SDIO_FUNCTION,
                          ESP_HOSTED_SCRATCH7_REG &
                          ESP_HOSTED_ADDRESS_MASK,
                          open, NULL);
  if (ret < 0)
    {
      return ret;
    }

  for (retries = 0; retries < ESP_HOSTED_RX_RETRIES; retries++)
    {
      interrupts = 0;
      ret = board_c6_cmd53_reg(dev, false, ESP_HOSTED_INT_RAW_REG,
                               &interrupts);
      if (ret < 0)
        {
          return ret;
        }

      if ((interrupts & ESP_HOSTED_NEW_PACKET) != 0)
        {
          break;
        }

      up_mdelay(10);
    }

  if (retries == ESP_HOSTED_RX_RETRIES)
    {
      return -ETIMEDOUT;
    }

  packet_len = 0;
  ret = board_c6_cmd53_reg(dev, false, ESP_HOSTED_PKT_LEN_REG,
                           &packet_len);
  if (ret < 0)
    {
      return ret;
    }

  packet_len &= ESP_HOSTED_RX_BYTE_MASK;
  if (packet_len < sizeof(*header) || packet_len > sizeof(g_c6_rxbuf))
    {
      return -EMSGSIZE;
    }

  nblocks = (packet_len + ESP_HOSTED_BLOCK_SIZE - 1) /
            ESP_HOSTED_BLOCK_SIZE;

  /* Clear the notification before draining the FIFO, matching the upstream
   * ESP-Hosted ordering and avoiding the loss of a new edge that arrives
   * while this packet is being consumed.
   */

  ret = board_c6_cmd53_reg(dev, true, ESP_HOSTED_INT_CLR_REG, &interrupts);
  if (ret < 0)
    {
      return ret;
    }

  memset(g_c6_rxbuf, 0xa5, packet_len);
  ret = sdio_io_rw_extended(dev, false, ESP_HOSTED_SDIO_FUNCTION,
                            ESP_HOSTED_FIFO_END - packet_len, true,
                            g_c6_rxbuf, ESP_HOSTED_BLOCK_SIZE, nblocks);
  if (ret < 0)
    {
      return ret;
    }

  header = (FAR struct esp_hosted_header_s *)g_c6_rxbuf;
  if ((header->interface & 0x0f) != ESP_HOSTED_PRIV_IF ||
      header->packet_type != ESP_HOSTED_PRIV_PKT_EVENT ||
      header->offset + sizeof(*event) > packet_len ||
      header->offset + header->len > packet_len)
    {
      return -EPROTO;
    }

  event = (FAR struct esp_hosted_priv_event_s *)
          (g_c6_rxbuf + header->offset);
  if (event->type != ESP_HOSTED_PRIV_EVT_INIT ||
      sizeof(*event) + event->len > header->len)
    {
      return -EPROTO;
    }

  g_c6_rx_byte_count = packet_len;
  g_c6_rx_offset = 0;
  g_c6_rx_pending = 0;
  g_c6_tx_buf_count = 0;
  g_c6_tx_sequence = 0;
  g_c6_rpc_uid = 0;
  syslog(LOG_INFO,
         "ESP32-C6 Hosted PASS: init event len=%u frame=%lu bytes\n",
         event->len, (unsigned long)packet_len);
  return OK;
}

static int board_c6_wifi_get_mac(FAR struct sdio_dev_s *dev,
                                 FAR uint8_t *mac)
{
  FAR const uint8_t *response;
  FAR const uint8_t *value;
  uint8_t request[8];
  size_t request_len = 0;
  size_t value_len;
  uint64_t status;
  int ret;

  ret = board_c6_pb_put_field(request, sizeof(request), &request_len, 1,
                              ESP_HOSTED_WIFI_IF_STA);
  if (ret < 0)
    {
      return ret;
    }

  ret = board_c6_rpc(dev, ESP_HOSTED_REQ_GET_MAC, request, request_len,
                     &response, &value_len);
  if (ret < 0)
    {
      return ret;
    }

  ret = board_c6_pb_find(response, value_len, 2, &status, NULL, NULL);
  if (ret != -ENOENT && (ret < 0 || (int32_t)status != 0))
    {
      return ret < 0 ? ret : -(int32_t)status;
    }

  ret = board_c6_pb_find(response, value_len, 1, NULL, &value, &value_len);
  if (ret < 0)
    {
      return ret;
    }

  if (value_len != 6)
    {
      return -EPROTO;
    }

  memcpy(mac, value, 6);
  return OK;
}

static int board_c6_net_ifup(FAR struct netdev_lowerhalf_s *lower)
{
  FAR struct board_c6_netdev_s *priv =
    container_of(lower, struct board_c6_netdev_s, lower);

  priv->ifup = true;
  if (priv->connected)
    {
      netdev_lower_carrier_on(lower);
    }

  return OK;
}

static int board_c6_net_ifdown(FAR struct netdev_lowerhalf_s *lower)
{
  FAR struct board_c6_netdev_s *priv =
    container_of(lower, struct board_c6_netdev_s, lower);

  priv->ifup = false;
  netdev_lower_carrier_off(lower);
  return OK;
}

static int board_c6_net_transmit(FAR struct netdev_lowerhalf_s *lower,
                                 FAR netpkt_t *pkt)
{
  unsigned int len;
  int ret;

  if (!g_c6_net.connected || g_c6_sdio == NULL)
    {
      return -ENETDOWN;
    }

  len = netpkt_getdatalen(lower, pkt);
  if (len == 0 || len > sizeof(g_c6_net_txpayload))
    {
      NETDEV_TXERRORS(&lower->netdev);
      return -EMSGSIZE;
    }

  ret = netpkt_copyout(lower, g_c6_net_txpayload, pkt, len, 0);
  if (ret < 0)
    {
      NETDEV_TXERRORS(&lower->netdev);
      return ret;
    }

  ret = nxmutex_lock(&g_c6_transport_lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = board_c6_hosted_send_data(g_c6_sdio, g_c6_net_txpayload, len);
  nxmutex_unlock(&g_c6_transport_lock);
  if (ret < 0)
    {
      NETDEV_TXERRORS(&lower->netdev);
      return ret;
    }

  NETDEV_TXPACKETS(&lower->netdev);
  netpkt_free(lower, pkt, NETPKT_TX);
  netdev_lower_txdone(lower);
  return OK;
}

static FAR netpkt_t *
board_c6_net_receive(FAR struct netdev_lowerhalf_s *lower)
{
  FAR struct board_c6_netdev_s *priv =
    container_of(lower, struct board_c6_netdev_s, lower);
  FAR netpkt_t *pkt;
  irqstate_t flags;

  flags = spin_lock_irqsave(&priv->lock);
  pkt = netpkt_remove_queue(&priv->rxqueue);
  spin_unlock_irqrestore(&priv->lock, flags);
  return pkt;
}

static const struct netdev_ops_s g_c6_net_ops =
{
  board_c6_net_ifup,
  board_c6_net_ifdown,
  board_c6_net_transmit,
  board_c6_net_receive
};

static int board_c6_net_enqueue(FAR const uint8_t *payload, size_t len)
{
  FAR struct netdev_lowerhalf_s *lower = &g_c6_net.lower;
  FAR netpkt_t *pkt;
  irqstate_t flags;
  int ret;

  if (!g_c6_net.registered || len < 14 || len > C6_NET_FRAME_MAX)
    {
      return -ENETDOWN;
    }

  pkt = netpkt_alloc(lower, NETPKT_RX);
  if (pkt == NULL)
    {
      NETDEV_RXDROPPED(&lower->netdev);
      return -ENOMEM;
    }

  ret = netpkt_copyin(lower, pkt, payload, len, 0);
  if (ret < 0)
    {
      netpkt_free(lower, pkt, NETPKT_RX);
      NETDEV_RXERRORS(&lower->netdev);
      return ret;
    }

  flags = spin_lock_irqsave(&g_c6_net.lock);
  ret = netpkt_tryadd_queue(pkt, &g_c6_net.rxqueue);
  spin_unlock_irqrestore(&g_c6_net.lock, flags);
  if (ret < 0)
    {
      netpkt_free(lower, pkt, NETPKT_RX);
      NETDEV_RXDROPPED(&lower->netdev);
      return ret;
    }

  NETDEV_RXPACKETS(&lower->netdev);
  netdev_lower_rxready(lower);
  return OK;
}

static void board_c6_net_set_connected(bool connected)
{
  g_c6_net.connected = connected;
  if (!g_c6_net.registered || !g_c6_net.ifup)
    {
      return;
    }

  if (connected)
    {
      netdev_lower_carrier_on(&g_c6_net.lower);
    }
  else
    {
      netdev_lower_carrier_off(&g_c6_net.lower);
    }
}

static int board_c6_net_rxthread(int argc, FAR char *argv[])
{
  FAR const uint8_t *payload;
  size_t payload_len;
  uint16_t msgid;
  uint32_t uid;
  unsigned int errors = 0;
  int ret;

  while (g_c6_net.running)
    {
      ret = nxmutex_lock(&g_c6_transport_lock);
      if (ret < 0)
        {
          break;
        }

      ret = board_c6_hosted_receive(g_c6_sdio, &msgid, &uid,
                                    &payload, &payload_len,
                                    C6_NET_RX_POLL_MS);
      nxmutex_unlock(&g_c6_transport_lock);

      if (ret == OK)
        {
          errors = 0;
          if (msgid == ESP_HOSTED_EVT_STA_CONN)
            {
              board_c6_net_set_connected(true);
              syslog(LOG_INFO, "ESP32-C6 Wi-Fi carrier connected\n");
            }
          else if (msgid == ESP_HOSTED_EVT_STA_DISCON)
            {
              board_c6_net_set_connected(false);
              syslog(LOG_ERR, "ERROR: ESP32-C6 Wi-Fi carrier lost\n");
            }

          /* Drain an aggregate promptly and release the transport lock
           * between frames so TCP ACK transmission is not starved.
           */

          continue;
        }
      else if (ret != -ETIMEDOUT)
        {
          errors++;
          if (errors == 1 || errors % 100 == 0)
            {
              syslog(LOG_ERR,
                     "ERROR: ESP32-C6 data receive failed: %d count=%u\n",
                     ret, errors);
            }
        }

      nxsig_usleep(C6_NET_RX_POLL_MS * 1000);
    }

  g_c6_net.rxpid = -1;
  return OK;
}

static int board_c6_net_register(FAR const uint8_t *mac)
{
  FAR struct netdev_lowerhalf_s *lower = &g_c6_net.lower;
  int ret;

  if (g_c6_net.registered)
    {
      return OK;
    }

  memset(&g_c6_net, 0, sizeof(g_c6_net));
  spin_lock_init(&g_c6_net.lock);
  lower->ops = &g_c6_net_ops;
  lower->quota[NETPKT_TX] = 1;
  lower->quota[NETPKT_RX] = C6_NET_RX_QUOTA;
  /* The ESP-Hosted transport already has its own RX polling thread.  Use a
   * dedicated upper-half thread to drain the netpkt queue when that poller
   * reports data.  NETDEV_RX_WORK expects lower->priority to contain a work
   * queue ID (HPWORK/LPWORK), not a scheduler priority; passing our RX task
   * priority there silently prevented the queued Ethernet frames from ever
   * reaching eth_input().
   */

  lower->rxtype = NETDEV_RX_THREAD;
  lower->priority = C6_NET_RX_PRIORITY;
  lower->netdev.d_pktsize = C6_NET_FRAME_MAX;
  memcpy(lower->netdev.d_mac.ether.ether_addr_octet, mac, 6);

  ret = netdev_lower_register(lower, NET_LL_ETHERNET);
  if (ret < 0)
    {
      return ret;
    }

  g_c6_net.registered = true;
  g_c6_net.running = true;
  g_c6_net.rxpid = kthread_create("c6-rx", C6_NET_RX_PRIORITY,
                                  C6_NET_RX_STACKSIZE,
                                  board_c6_net_rxthread, NULL);
  if (g_c6_net.rxpid < 0)
    {
      ret = g_c6_net.rxpid;
      g_c6_net.running = false;
      g_c6_net.registered = false;
      netdev_lower_unregister(lower);
      return ret;
    }

  syslog(LOG_INFO,
         "ESP32-C6 netdev PASS: %s MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
         lower->netdev.d_ifname,
         mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int board_c6_wifi_initialize(void)
{
  uint16_t vendor = 0;
  uint16_t device = 0;
  uint32_t scratch0 = 0;
  uint8_t cccr_rev = 0;
  int ret;

  if (g_c6_ready)
    {
      return OK;
    }

  syslog(LOG_INFO, "ESP32-C6 SDIO: reset and host initialization\n");
  g_c6_ready = false;
  board_c6_reset();

  g_c6_sdio = esp32p4_sdmmc_sdio_initialize(BOARD_C6_SDIO_SLOT);
  if (g_c6_sdio == NULL)
    {
      return -ENODEV;
    }

  /* The P4 SDMMC command path is polled.  Do not allocate its peripheral
   * interrupt during link discovery: on the current P4 CLIC integration,
   * routing this source can stall while leaving the probe itself usable.
   */

  SDIO_CLOCK(g_c6_sdio, CLOCK_IDMODE);

  syslog(LOG_INFO, "ESP32-C6 SDIO: probing function 0\n");
  ret = sdio_probe(g_c6_sdio);
  if (ret < 0)
    {
      return ret;
    }

  /* Follow the ESP-IDF SDIO-card initialization order before enabling
   * Function 1: switch the CCCR and host to 4-bit mode, then raise CCLK to
   * the card's default 20 MHz transfer rate.  The ESP32-C6 Hosted slave
   * applies IOEN correctly in this negotiated transfer mode.
   */

  ret = sdio_set_wide_bus(g_c6_sdio);
  if (ret < 0)
    {
      return ret;
    }

  SDIO_CLOCK(g_c6_sdio, CLOCK_SD_TRANSFER_4BIT);

  ret = sdio_io_rw_direct(g_c6_sdio, false, 0, SDIO_CCCR_REV, 0,
                          &cccr_rev);
  if (ret < 0)
    {
      return ret;
    }

  ret = board_c6_read_manfid(g_c6_sdio, &vendor, &device);
  if (ret < 0)
    {
      syslog(LOG_ERR,
             "ERROR: ESP32-C6 SDIO CIS read failed: CCCR=0x%02x ret=%d\n",
             cccr_rev, ret);
      return ret;
    }

  syslog(LOG_INFO,
         "ESP32-C6 SDIO CIS PASS: CCCR=0x%02x vendor=0x%04x "
         "device=0x%04x\n",
         cccr_rev, vendor, device);

  ret = board_c6_cmd53_dma_probe(g_c6_sdio, &scratch0);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: ESP32-C6 CMD53 DMA probe failed: %d\n", ret);
      return ret;
    }

  syslog(LOG_INFO,
         "ESP32-C6 SDIO DMA PASS: CMD53 scratch0=0x%08lx\n",
         (unsigned long)scratch0);

  ret = board_c6_hosted_handshake(g_c6_sdio);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: ESP32-C6 Hosted handshake failed: %d\n",
             ret);
      return ret;
    }

  g_c6_ready = true;
  return OK;
}

int board_c6_wifi_version(FAR uint32_t *major, FAR uint32_t *minor,
                          FAR uint32_t *patch)
{
  FAR const uint8_t *response;
  size_t response_len;
  uint64_t value;
  int ret;

  ret = board_c6_wifi_initialize();
  if (ret < 0)
    {
      return ret;
    }

  ret = board_c6_rpc(g_c6_sdio, 350, NULL, 0,
                     &response, &response_len);
  if (ret < 0)
    {
      return ret;
    }

  value = 0;
  ret = board_c6_pb_find(response, response_len, 1, &value, NULL, NULL);
  if (ret != -ENOENT && (ret < 0 || (int32_t)value != 0))
    {
      return ret < 0 ? ret : -(int32_t)value;
    }

  ret = board_c6_pb_find(response, response_len, 2, &value, NULL, NULL);
  if (ret < 0)
    {
      return ret;
    }

  *major = value;
  ret = board_c6_pb_find(response, response_len, 3, &value, NULL, NULL);
  if (ret < 0)
    {
      return ret;
    }

  *minor = value;
  ret = board_c6_pb_find(response, response_len, 4, &value, NULL, NULL);
  if (ret < 0)
    {
      return ret;
    }

  *patch = value;
  return OK;
}

int board_c6_wifi_connect(FAR const char *ssid, FAR const char *password)
{
  uint8_t request[8];
  uint8_t mac[6];
  size_t request_len = 0;
  int ret;

  if (ssid == NULL || password == NULL || strlen(ssid) == 0 ||
      strlen(ssid) > ESP_HOSTED_SSID_MAX ||
      strlen(password) > ESP_HOSTED_PASSWORD_MAX)
    {
      return -EINVAL;
    }

  ret = board_c6_wifi_initialize();
  if (ret < 0)
    {
      return ret;
    }

  ret = board_c6_wifi_rpc_init(g_c6_sdio);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: ESP32-C6 RPC init failed: %d\n", ret);
      return ret;
    }

  ret = board_c6_pb_put_field(request, sizeof(request), &request_len, 1,
                              ESP_HOSTED_WIFI_MODE_STA);
  if (ret < 0)
    {
      return ret;
    }

  ret = board_c6_rpc_status(g_c6_sdio, ESP_HOSTED_REQ_SET_MODE,
                            request, request_len);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: ESP32-C6 set STA mode failed: %d\n", ret);
      return ret;
    }

  ret = board_c6_wifi_get_mac(g_c6_sdio, mac);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: ESP32-C6 get STA MAC failed: %d\n", ret);
      return ret;
    }

  ret = board_c6_wifi_rpc_set_config(g_c6_sdio, ssid, password);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: ESP32-C6 set STA config failed: %d\n", ret);
      return ret;
    }

  ret = board_c6_rpc_status(g_c6_sdio, ESP_HOSTED_REQ_WIFI_START, NULL, 0);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: ESP32-C6 Wi-Fi start failed: %d\n", ret);
      return ret;
    }

  ret = board_c6_rpc_status(g_c6_sdio, ESP_HOSTED_REQ_WIFI_CONN, NULL, 0);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: ESP32-C6 STA connect request failed: %d\n",
             ret);
      return ret;
    }

  ret = board_c6_wifi_wait_connected(g_c6_sdio);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: ESP32-C6 STA association failed: %d\n", ret);
      return ret;
    }

  ret = board_c6_net_register(mac);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: ESP32-C6 netdev register failed: %d\n", ret);
      return ret;
    }

  board_c6_net_set_connected(true);
  return OK;
}

FAR struct sdio_dev_s *board_c6_wifi_sdio(void)
{
  return g_c6_sdio;
}

FAR const char *board_c6_wifi_netdev_name(void)
{
  return g_c6_net.registered ? g_c6_net.lower.netdev.d_ifname : NULL;
}
