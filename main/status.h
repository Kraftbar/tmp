#pragma once

enum status_mode {
    STATUS_BOOTING = 0,
    STATUS_RUNNING,
    STATUS_ERROR,
};

void status_init(void);
void status_set_mode(enum status_mode mode);
void status_poll(void);
