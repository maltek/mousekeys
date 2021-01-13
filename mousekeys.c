#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* produced through ample copy/paste from evtest.c and uinput documentation ->
 * GPLv2 */

static int event_type = EV_KEY;
struct btn_action
{
  int btn;    // the mouse button to act on
  int keys[]; // the keys to press (ending with a zero)
};
static struct btn_action action_right = {
  .btn = BTN_EXTRA,
  .keys = { KEY_LEFTCTRL, KEY_PAGEDOWN, 0 }
};
static struct btn_action action_left = {
  .btn = BTN_SIDE,
  .keys = { KEY_LEFTCTRL, KEY_PAGEUP, 0 }
};
static struct btn_action* output_codes[] = { &action_right, &action_left };

static int uinput_fd = -1;

static int
make_keyboard(void)
{
  struct uinput_setup usetup = {
        .id = {
            .bustype = BUS_USB,
            .vendor = 0x1234, // sample vendor
            .product = 0x5678, // sample product
        },
        .name = "MouseMapperPseudoDevice",
    };

  uinput_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
  if (uinput_fd == -1)
    return -1;

  /*
   * The ioctls below will enable the device that is about to be
   * created, to pass key events.
   */
  int ret = ioctl(uinput_fd, UI_SET_EVBIT, EV_KEY);
  if (ret == -1)
    return -1;
  for (size_t i = 0; i < sizeof(output_codes) / sizeof(output_codes[0]); i++) {
    for (size_t j = 0; output_codes[i]->keys[j] != 0; j++) {
      ret = ioctl(uinput_fd, UI_SET_KEYBIT, output_codes[i]->keys[j]);
      if (ret == -1)
        return -1;
    }
  }

  ret = ioctl(uinput_fd, UI_DEV_SETUP, &usetup);
  if (ret == -1)
    return -1;
  ret = ioctl(uinput_fd, UI_DEV_CREATE);
  if (ret == -1)
    return -1;

  return 0;
}
static void
destroy_keyboard(void)
{
  ioctl(uinput_fd, UI_DEV_DESTROY);
  close(uinput_fd);
  uinput_fd = -1;
}

static int
write_all(int fd, const void* buf, size_t count)
{
  int num = 0;
  while (count > 0) {
    num = write(fd, buf, count);
    if (num == -1)
      return -1;
    count -= num;
    buf += num;
  }
  return 0;
}
static void
put_event(const struct input_event* ev)
{
  write_all(uinput_fd, ev, sizeof(*ev));
  // some apps (Slack) eat keypresses when they happen too quickly
  usleep(1000);
}

static void
press_keys(struct btn_action* what)
{
  for (size_t i = 0; what->keys[i] != 0; i++) {
    put_event(&(struct input_event){
      .type = EV_KEY, .code = what->keys[i], .value = 1 });
  }
  put_event(
    &(struct input_event){ .type = EV_SYN, .code = SYN_REPORT, .value = 0 });

  for (size_t i = 0; what->keys[i] != 0; i++) {
    put_event(&(struct input_event){
      .type = EV_KEY, .code = what->keys[i], .value = 0 });
  }
  put_event(
    &(struct input_event){ .type = EV_SYN, .code = SYN_REPORT, .value = 0 });
}

/**
 * Grab and immediately ungrab the device.
 *
 * @param fd The file descriptor to the device.
 * @return 0 if the grab was successful, or 1 otherwise.
 */
static int
test_grab(int fd)
{
  int rc;

  rc = ioctl(fd, EVIOCGRAB, (void*)1);

  if (rc == 0)
    ioctl(fd, EVIOCGRAB, (void*)0);

  return rc;
}

static volatile sig_atomic_t stop = 0;

static void
interrupt_handler(int sig)
{
  (void)sig;
  stop = 1;
}

/**
 * Print device events as they come in.
 *
 * @param fd The file descriptor to the device.
 * @return 0 on success or 1 otherwise.
 */
static int
rewrite_events(int fd)
{
  struct input_event ev[64];
  int rd;
  fd_set rdfs;

  FD_ZERO(&rdfs);
  FD_SET(fd, &rdfs);

  while (!stop) {
    select(fd + 1, &rdfs, NULL, NULL, NULL);
    if (stop)
      break;

    rd = read(fd, ev, sizeof(ev));

    if (rd < (int)sizeof(struct input_event)) {
      int e = errno;
      perror("\nmousekeys: error reading");
      if (e == ENODEV)
        return EXIT_SUCCESS;
      fprintf(stderr,
              "expected %d bytes, got %d\n",
              (int)sizeof(struct input_event),
              rd);
      return EXIT_FAILURE;
    }

    for (size_t i = 0; i < rd / sizeof(struct input_event); i++) {
      if (ev[i].type == event_type && ev[i].value == 1) {
        for (size_t j = 0; j < sizeof(output_codes) / sizeof(output_codes[0]);
             j++) {
          if (ev[i].code == output_codes[j]->btn) {
            press_keys(output_codes[j]);
            break;
          }
        }
      }
    }
  }

  return EXIT_SUCCESS;
}

int
main(int argc, char** argv)
{
  int ret = EXIT_FAILURE;
  int fd = -1;
  if (argc != 2) {
    fprintf(stderr, "Usage: %s devicepath\n", argv[0]);
    goto error;
  }

  char* device = argv[1];

  if ((fd = open(device, O_RDONLY | O_NONBLOCK)) < 0) {
    perror(device);
    if (errno == EACCES && getuid() != 0)
      fprintf(stderr,
              "You do not have access to %s. Try "
              "running %s as root instead.\n",
              device,
              argv[0]);
    goto error;
  }

  if (test_grab(fd)) {
    printf("***********************************************\n");
    printf("  This device is grabbed by another process.\n");
    printf("  No events are available to %s while the other\n"
           "  grab is active.\n",
           argv[0]);
    printf("  In most cases, this is caused by an X driver,\n"
           "  try VT-switching and re-run %s again.\n",
           argv[0]);
    printf("  Run the following command to see processes with\n"
           "  an open fd on this device\n"
           " \"fuser -v %s\"\n",
           device);
    printf("***********************************************\n");
  }

  if (make_keyboard()) {
    fprintf(stderr, "Failed to create uinput keyboard device.\n");
    goto error;
  }

  signal(SIGINT, interrupt_handler);
  signal(SIGTERM, interrupt_handler);

  ret = rewrite_events(fd);

error:
  if (fd != -1)
    close(fd);
  if (uinput_fd != -1)
    destroy_keyboard();

  return ret;
}
