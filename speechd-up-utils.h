#ifndef SPEECHDUP_UTILS_H
#define SPEECHDUP_UTILS_H

/* Translate a Speech Dispatcher numeric value in [-100,100] into
 * a SpeakUp digit (0..9) and a base (0..20). The caller must provide
 * pointers for ret and base.
 */
void get_initial_speakup_value(int num, int *ret, int *base);

#endif