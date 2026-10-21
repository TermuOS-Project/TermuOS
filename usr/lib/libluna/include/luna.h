#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int luna_connect(void);
void luna_disconnect(void);

int luna_window_create(const char *title, int w, int h);
int luna_window_show(int id);
int luna_window_close(int id);

#ifdef __cplusplus
}
#endif
