#ifndef UI_H
#define UI_H

#include "app.h"

/* Runs the interactive menus for an authenticated administrator. */
void ui_run_menus(AppData *app, const AdminSession *session);

#endif
