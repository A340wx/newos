/*
** Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
** Distributed under the terms of the NewOS License.
*/
#include <kernel/kernel.h>
#include <kernel/debug.h>
#include <kernel/heap.h>
#include <kernel/int.h>
#include <kernel/sem.h>
#include <kernel/module.h>
#include <kernel/lock.h>
#include <kernel/vm.h>
#include <kernel/fs/devfs.h>
#include <kernel/arch/cpu.h>
#include <string.h>
#include <newos/errors.h>

#include <newos/key_event.h>

#include <kernel/bus/isa/isa.h>

static int  leds;
static sem_id keyboard_sem;
static mutex keyboard_read_mutex;
#define BUF_LEN 256
const int keyboard_buf_len = BUF_LEN;
static _key_event keyboard_buf[BUF_LEN];
static unsigned int head, tail;
static isa_bus_manager *isa;

const uint16 pc_keymap_set1[128] = {
	/* 0x00 */ 0, KEY_ESC, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', KEY_BACKSPACE, KEY_TAB,
	/* 0x10 */ 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', KEY_RETURN, KEY_LCTRL, 'a', 's',
	/* 0x20 */ 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', KEY_LSHIFT, '\\', 'z', 'x', 'c', 'v',
	/* 0x30 */ 'b', 'n', 'm', ',', '.', '/', KEY_RSHIFT, '*', KEY_LALT, ' ', KEY_CAPSLOCK, KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5,
	/* 0x40 */ KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_PAD_NUMLOCK, KEY_SCRLOCK, KEY_PAD_7, KEY_PAD_8, KEY_PAD_9, KEY_PAD_MINUS, KEY_PAD_4, KEY_PAD_5, KEY_PAD_6, KEY_PAD_PLUS, KEY_PAD_1,
	/* 0x50 */ KEY_PAD_2, KEY_PAD_3, KEY_PAD_0, KEY_PAD_PERIOD, 0, 0, 0, KEY_F11, KEY_F12, 0, 0, 0, 0, 0, 0, 0,
};

const uint16 pc_keymap_set1_e0[128] = {
	/* 0x00 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0x10 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, KEY_PAD_ENTER, KEY_RCTRL, 0, 0,
	/* 0x20 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0x30 */ 0, 0, 0, 0, 0, KEY_PAD_DIVIDE, 0, KEY_PRTSCRN, KEY_RALT, 0, 0, 0, 0, 0, 0, 0,
	/* 0x40 */ 0, 0, 0, 0, 0, 0, 0, KEY_HOME, KEY_ARROW_UP, KEY_PGUP, 0, KEY_ARROW_LEFT, 0, KEY_ARROW_RIGHT, 0, KEY_END,
	/* 0x50 */ KEY_ARROW_DOWN, KEY_PGDN, KEY_INS, 0, 0, 0, 0, 0, 0, 0, 0, KEY_LWIN, KEY_RWIN, KEY_MENU, 0, 0
};

static void wait_for_output(void)
{
	while(isa->read_io_8(0x64) & 0x2)
		;
}

static void set_leds(void)
{
	wait_for_output();
	isa->write_io_8(0x60, 0xed);
	wait_for_output();
	isa->write_io_8(0x60, leds);
}

static int _keyboard_read(_key_event *buf, size_t len)
{
	unsigned int saved_tail;
	size_t copied_events = 0;
	size_t copy_len;
	int rc;

	/* clamp the input len value */
	len = min(len, keyboard_buf_len - 1);

retry:
	// block here until data is ready
	rc = sem_acquire_etc(keyboard_sem, 1, SEM_FLAG_INTERRUPTABLE, 0, NULL);
	if(rc == ERR_INTERRUPTED) {
		return 0;
	}

	// critical section
	mutex_lock(&keyboard_read_mutex);

	saved_tail = tail;
	if(head == saved_tail) {
		mutex_unlock(&keyboard_read_mutex);
		goto retry;
	} else {
		// copy out of the buffer
		if(head < saved_tail)
			copy_len = min(len, saved_tail - head);
		else
			copy_len = min(len, keyboard_buf_len - head);
		memcpy(buf, &keyboard_buf[head], copy_len * sizeof(_key_event));
		copied_events = copy_len;
		head = (head + copy_len) % keyboard_buf_len;
		if(head == 0 && saved_tail > 0 && copied_events < len) {
			// we wrapped around and have more bytes to read
			// copy the first part of the buffer
			copy_len = min(saved_tail, len - copied_events);
			memcpy(&buf[copied_events], &keyboard_buf[0], copy_len * sizeof(_key_event));
			copied_events += copy_len;
			head = copy_len;
		}
	}
	if(head != saved_tail) {
		// we did not empty the keyboard queue
		sem_release_etc(keyboard_sem, 1, SEM_FLAG_NO_RESCHED);
	}

	mutex_unlock(&keyboard_read_mutex);

	return copied_events;
}

static void insert_in_buf(_key_event *event)
{
	unsigned int temp_tail = tail;

 	// see if the next char will collide with the head
	temp_tail++;
	temp_tail %= keyboard_buf_len;
	if(temp_tail == head) {
		// buffer overflow, ditch this char
		return;
	}
	keyboard_buf[tail].keycode = event->keycode;
	keyboard_buf[tail].modifiers = event->modifiers;
	tail = temp_tail;
	sem_release_etc(keyboard_sem, 1, SEM_FLAG_NO_RESCHED);
}

static int handle_set1_keycode(unsigned char key)
{
	int retval = INT_NO_RESCHEDULE;
	_key_event event;
	bool queue_event = true; // we will by default queue the key
	static bool seen_e0 = false;
	static bool seen_e1 = false;

	event.modifiers = 0;

	// look for special keys first
	if(key == 0xe0) {
		seen_e0 = true;
//		dprintf("handle_set1_keycode: e0 prefix\n");
		queue_event = false;
	} else if(key == 0xe1) {
		seen_e1 = true;
//		dprintf("handle_set1_keycode: e1 prefix\n");
		queue_event = false;
	} else if(seen_e0) {
		// this is the character after e0
		if(key & 0x80) {
			event.modifiers |= KEY_MODIFIER_UP;
			key &= 0x7f; // mask out the top bit
		} else {
			event.modifiers |= KEY_MODIFIER_DOWN;
		}

		// do the keycode lookup
		event.keycode = pc_keymap_set1_e0[key];

		if(event.keycode == 0) {
			queue_event = false;
		}
		seen_e0 = false;
	} else if(seen_e1) {
		seen_e1 = false;
	} else {
		// it was a regular key
		if(key & 0x80) {
			event.modifiers |= KEY_MODIFIER_UP;
			key &= 0x7f; // mask out the top bit
		} else {
			event.modifiers |= KEY_MODIFIER_DOWN;
		}

		// do the keycode lookup
		event.keycode = pc_keymap_set1[key];

		// by default we're gonna queue this thing

		if(event.keycode == 0) {
			// some invalid key
//			dprintf("handle_set1_keycode: got invalid raw key 0x%x\n", key);
			queue_event = false;
		}
	}

	if(queue_event) {
		// do some special checks here
		switch(event.keycode) {
			// special stuff
			case KEY_PRTSCRN:
				panic("Keyboard Requested Halt\n");
				break;
			case KEY_F12:
				reboot();
				break;
			case KEY_SCRLOCK:
				if(event.modifiers & KEY_MODIFIER_DOWN)
					dbg_set_serial_debug(dbg_get_serial_debug()?false:true);
				break;
		}

		insert_in_buf(&event);
		retval = INT_RESCHEDULE;
	}

	return retval;
}

static int handle_keyboard_interrupt(void* data)
{
	unsigned char key;

	key = isa->read_io_8(0x60);
//	dprintf("handle_keyboard_interrupt: key = 0x%x\n", key);

	return handle_set1_keycode(key);
}

static int keyboard_open(dev_ident ident, dev_cookie *cookie)
{
	*cookie = NULL;
	return 0;
}

static int keyboard_close(dev_cookie cookie)
{
	return 0;
}

static int keyboard_freecookie(dev_cookie cookie)
{
	return 0;
}

static int keyboard_seek(dev_cookie cookie, off_t pos, seek_type st)
{
	return ERR_NOT_ALLOWED;
}

static ssize_t keyboard_read(dev_cookie cookie, void *buf, off_t pos, ssize_t len)
{
	if(len < 0)
		return 0;

	return _keyboard_read(buf, (size_t)(len / sizeof(_key_event))) * sizeof(_key_event);
}

static ssize_t keyboard_write(dev_cookie cookie, const void *buf, off_t pos, ssize_t len)
{
	return ERR_VFS_READONLY_FS;
}

static int keyboard_ioctl(dev_cookie cookie, int op, void *buf, size_t len)
{
	switch(op) {
		case _KEYBOARD_IOCTL_SET_LEDS:
			user_memcpy(&leds, buf, sizeof(leds));
			set_leds();
			return 0;
		default:
			return ERR_INVALID_ARGS;
	}
}

struct dev_calls keyboard_hooks = {
	&keyboard_open,
	&keyboard_close,
	&keyboard_freecookie,
	&keyboard_seek,
	&keyboard_ioctl,
	&keyboard_read,
	&keyboard_write,
	/* cannot page from keyboard */
	NULL,
	NULL,
	NULL
};

static int setup_keyboard(void)
{
	keyboard_sem = sem_create(0, "keyboard_sem");
	if(keyboard_sem < 0)
		panic("could not create keyboard sem!\n");

	if(mutex_init(&keyboard_read_mutex, "keyboard_read_mutex") < 0)
		panic("could not create keyboard read mutex!\n");

	leds = 0;
	set_leds();

	head = tail = 0;

	return 0;
}

int	dev_bootstrap(void);

int	dev_bootstrap(void)
{
	if(module_get(ISA_MODULE_NAME, 0, (void **)&isa) < 0) {
		dprintf("keyboard dev_bootstrap: no isa bus found..\n");
		return -1;
	}

	setup_keyboard();
	int_set_io_interrupt_handler(1,&handle_keyboard_interrupt, NULL, "keyb");

	devfs_publish_device("keyboard", NULL, &keyboard_hooks);

	return NO_ERROR;
}
