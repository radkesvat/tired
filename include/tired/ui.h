#ifndef TIRED_UI_H
#define TIRED_UI_H
#include "tired/mutation.h"
#include <signal.h>
extern volatile sig_atomic_t tired_ui_signal;
/* ncursesw review edits the same typed model used by headless operation. Never
 * writes service files or performs manager mutations. Always restores terminal
 * state. false represents cancellation/terminal/validation errors. */
bool tired_ui_review(TiredMutation *mutation, const TiredLayout *layout,
                     const TiredBackend *backend, const TiredSettings *settings, bool no_tui,
                     bool monochrome, TiredError *error);
/* Reevaluate the retained profile after changes to condition inputs. Restores
 * generic/configured defaults beneath profile values; preserves explicit choices. */
bool tired_ui_review_recompute(TiredMutation *mutation, const TiredBackend *backend,
                               const TiredSettings *settings, TiredError *error);
bool tired_ui_usable(void);
void tired_ui_presentation(bool ascii);
/* Wait for buffered input, terminal input, resize or a signal. A negative
 * timeout blocks until an event; collectors use a bounded positive timeout. */
int tired_ui_key(int timeout_ms);
/* Draw a resize message when the current curses surface is too small. Callers
 * retain editor state and accept only cancellation/resize until this is true. */
bool tired_ui_ready(int minimum_rows, int minimum_columns);
bool tired_ui_field_evidence(const TiredMutation *mutation, TiredFieldId field, TiredText *output,
                             TiredError *error);
bool tired_ui_dashboard(const TiredRequest *request, const char *profiles, TiredStatus *status,
                        TiredError *error);
/* Three factual detail lines for a managed-list row, independent of curses. */
bool tired_ui_dashboard_details(struct json_object *entry, TiredText *output, TiredError *error);
/* Internal viewers for an already active single-threaded curses session. */
void tired_ui_draw(int row, int column, const char *text, int maximum);
void tired_ui_text_view(const char *title, const char *bytes);
void tired_ui_inputs(TiredMutation *mutation, bool credentials, TiredError *error);
void tired_ui_edit_list(TiredMutation *mutation, TiredFieldId field, TiredError *error);
void tired_ui_diff(const TiredMutation *mutation, const TiredLayout *layout, TiredError *error);
bool tired_ui_text_prompt(const char *title, const char *initial, TiredText *output,
                          TiredError *error);
#endif
