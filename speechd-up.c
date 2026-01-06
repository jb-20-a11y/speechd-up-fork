
/*
 * speechd-up.c - Simple interface between SpeakUp and Speech Dispatcher
 *
 * Copyright (C) 2004, 2006, 2007 Brailcom, o.p.s.
 *
 * This is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this package; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <stdarg.h>
#include <signal.h>
#include <ctype.h>
#include <locale.h>

#include <wchar.h>
#include <wctype.h>

#include <iconv.h>
#include <speech-dispatcher/libspeechd.h>

#include "log.h"
#include "options.h"
#include "configuration.h"
#include "speechd-up-utils.h"

#define BUF_SIZE 1024

#define DTLK_STOP 24
#define DTLK_CMD 1

extern struct spd_options options;

int fd;
fd_set fd_list;
fd_set fd_write;
SPDConnection *conn;

char *spd_spk_pid_file;

// BEGIN workaround raw speech-dispatcher access
static char *get_param_str(char *reply, int num, int *err)
{
	int i;
	char *tptr;
	char *pos;
	char *pos_begin;
	char *pos_end;
	char *rep;

	assert(err != NULL);

	if (num < 1) {
		*err = -1;
		return NULL;
	}

	pos = reply;
	for (i = 0; i <= num - 2; i++) {
		pos = strstr(pos, "\r\n");
		if (pos == NULL) {
			*err = -2;
			return NULL;
		}
		pos += 2;
	}

	if (strlen(pos) < 4)
		return NULL;

	*err = strtol(pos, &tptr, 10);
	if (*err >= 300 && *err <= 399)
		return NULL;

	if ((*tptr != '-') || (tptr != pos + 3)) {
		*err = -3;
		return NULL;
	}

	pos_begin = pos + 4;
	pos_end = strstr(pos_begin, "\r\n");
	if (pos_end == NULL) {
		*err = -4;
		return NULL;
	}

	rep = (char *)strndup(pos_begin, pos_end - pos_begin);
	*err = 0;
	LOG(5, "%s", rep);
	return rep;
}

char* get_voice_type(SPDConnection * connection)
{
	char *ret;
	int err;
	char *reply = NULL;
	spd_execute_command_with_reply(connection, "GET voice_type", &reply);
	ret = get_param_str(reply, 1, &err);
	free(reply);
	return ret;
}
// END workaround speech-dispatcher access


// Constant arrays
const SPDPunctuation punc_types[] = {SPD_PUNCT_ALL, SPD_PUNCT_SOME, SPD_PUNCT_NONE};
const int punc_types_count = 3;
const SPDVoiceType voice_types[] = {SPD_MALE1, SPD_MALE2, SPD_MALE3, SPD_FEMALE1, SPD_FEMALE2, SPD_FEMALE3, SPD_CHILD_MALE, SPD_CHILD_FEMALE};
const char* voice_types_str[] = {"MALE1", "MALE2", "MALE3", "FEMALE1", "FEMALE2", "FEMALE3", "CHILD_MALE", "CHILD_FEMALE"};
const int voice_types_count = 8;

// Current values
int currate = 5;
int curratebase = -100;
int curpitch = 5;
int curpitchbase = -100;
int curvol = 5;
int curvolbase = -100;
int curvoicenum = 0;
SPDVoiceType curvoice = SPD_MALE1;
int curpuncnum = 0;
SPDPunctuation curpunc = SPD_PUNCT_ALL;
int safe_to_change_punc = 0;

const char* SPEAKUP_PITCH = "/sys/accessibility/speakup/soft/pitch";
const char* SPEAKUP_PUNCTUATION = "/sys/accessibility/speakup/soft/punct";
const char* SPEAKUP_RATE = "/sys/accessibility/speakup/soft/rate";
const char* SPEAKUP_VOICE = "/sys/accessibility/speakup/soft/voice";
const char* SPEAKUP_VOLUME = "/sys/accessibility/speakup/soft/vol";

int get_speakup_option(const char* file_location)
{
	int digit = 0;
	FILE *pFile;
	pFile = fopen (file_location, "r");
	if (pFile == NULL) {
		LOG(1, "Could not open for reading: %s", file_location);
		return 0;
	}
	else {
		fscanf(pFile, "%d", &digit);
		fclose(pFile);
	}
	return digit;
}

void set_speakup_option(const char* file_location, int digit)
{
	FILE *pFile = fopen(file_location, "w");
	if (pFile == NULL) {
		LOG(1, "Could not open for writing: %s", file_location);
		return;
	};
	fprintf(pFile, "%d\n", digit);
	fclose(pFile);
}

void spd_update_variables()
{
currate = get_speakup_option(SPEAKUP_RATE);
curpitch = get_speakup_option(SPEAKUP_PITCH);
//curvol = get_speakup_option(SPEAKUP_VOLUME);
curvoicenum = get_speakup_option(SPEAKUP_VOICE);
//curpunc = get_speakup_option(SPEAKUP_PUNCTUATION);
}

void spd_sync_defaults()
{
	LOG(5,"Attempting to synchronize speechd-up to inherited speech-dispatcher defaults\n");
	int rate = spd_get_voice_rate(conn);
	get_initial_speakup_value(rate, &currate, &curratebase);
	LOG(5,"Default rate %d translated to speakup value %d with base %d\n", rate, currate, curratebase);
	set_speakup_option(SPEAKUP_RATE, currate);
	int pitch = spd_get_voice_pitch(conn);
	get_initial_speakup_value(pitch, &curpitch, &curpitchbase);
	LOG(5,"Default pitch %d translated to speakup value %d with base %d\n", pitch, curpitch, curpitchbase);
	set_speakup_option(SPEAKUP_PITCH, curpitch);
	int vol = spd_get_volume(conn);
	get_initial_speakup_value(vol, &curvol, &curvolbase);
	LOG(5,"Default volume %d translated to speakup value %d with base %d\n", vol, curvol, curvolbase);
	set_speakup_option(SPEAKUP_VOLUME, curvol);
	char *curvoicestr = get_voice_type(conn);
	for (int i = 0; i < voice_types_count; ++i) {
		if (strcmp(voice_types_str[i], curvoicestr) == 0) {
			curvoicenum = i;
			curvoice = voice_types[i];
		}
	}
	LOG(5,"Default voice type %d\n", curvoicenum);
	set_speakup_option(SPEAKUP_VOICE, curvoicenum);
	/* no way to get punctuation
	curpunc = spd_get_punctuation(conn);
	for (int i = 0; i < punc_types_count; ++i) {
		if (punc_types[i] == curpunc) {
			curpuncnum = i;
		}
	}
	LOG(5,"Default punctuation type %d\n", curpunc);
	set_speakup_option(SPEAKUP_PUNCTUATION, curpuncnum);
	*/
}

void init_ssml_char_escapes(void);
void spd_spk_reset(int sig);

/* Lifted directly from speechd/src/modules/module_utils.c. */
void xfree(void *data)
{
	if (data != NULL)
		free(data);
}

void
index_marker_callback(size_t msg_id, size_t client_id, SPDNotificationType type,
		      char *index_mark)
{
	//LOG(5,"Index Mark Callback");
	if (index_mark != NULL)
		if (write(fd, index_mark, sizeof(index_mark)) < 0)
			LOG(1, "Unable to write index mark: %s\n",
			    strerror(errno));
}

void speechd_init()
{
	conn = spd_open("speakup", "softsynth", "unknown", SPD_MODE_THREADED);
	if (conn == 0)
		FATAL(1, "ERROR! Can't connect to Speech Dispatcher!");
	conn->callback_im = index_marker_callback;
	if (spd_set_notification_on(conn, SPD_INDEX_MARKS) == -1)
		LOG(1, "Error turning on Index Mark Callback");

	if (options.language_set != DEFAULT)
		if (spd_set_language(conn, options.language) == -1)
			LOG(1, "Error setting language");

	if (spd_set_capital_letters(conn, SPD_CAP_NONE) == -1)
		LOG(1, "Unable to set capital letter recognition");

	init_ssml_char_escapes();
	if (options.respect_spd_defaults == 1)
		spd_sync_defaults();
	//spd_update_variables();
}

void speechd_close()
{
	spd_close(conn);
}

int init_speakup_tables()
{
	FILE *fp_char = fopen(options.speakup_characters, "w");
	if (fp_char) {
		int i = 0;

		fprintf(fp_char, "32 space\n");

		for (i = 33; i < 256; i++) {

			fprintf(fp_char, "%d %c\n", i, i);
		}
		fclose(fp_char);
	} else
		return -1;

	fp_char = fopen(options.speakup_chartab, "w");
	if (fp_char) {
		int i = 0;
		for (i = 'a'; i <= 'z'; i++) {
			fprintf(fp_char, "%d\tALPHA\n", i);
		}
		for (i = 'A'; i <= 'Z'; i++) {
			fprintf(fp_char, "%d\tA_CAP\n", i);
		}
		for (i = 128; i < 256; i++) {
			fprintf(fp_char, "%d\tALPHA\n", i);
		}
		fclose(fp_char);
	} else
		return -1;

	return 0;
}

void process_command(char command, unsigned int param, int pm)
{
	int val, ret = 0;

	LOG(5, "cmd: %c, param: %d, rel: %d", command, param, pm);
	if (pm != 0)
		pm *= param;

	switch (command) {

	case '@':		/* Reset speechd connection */
		LOG(5, "resetting speech dispatcher connection");
		spd_spk_reset(0);
		break;

	case 'b':		/* set punctuation level */
		if (options.respect_spd_defaults == 0 || safe_to_change_punc == 1) {
			switch (param) {
			case 0:
				LOG(5, "[punctuation all]");
				ret = spd_set_punctuation(conn, SPD_PUNCT_ALL);
				break;
			case 1:
				LOG(5, "[punctuation some]");
				ret = spd_set_punctuation(conn, SPD_PUNCT_SOME);
				break;
			case 2:
				LOG(5, "[punctuation none]");
				ret = spd_set_punctuation(conn, SPD_PUNCT_NONE);
				break;
			default:
				LOG(1, "ERROR: Invalid punctuation mode!");
			}
			if (ret == -1)
				LOG(1, "ERROR: Can't set punctuation mode");
			else {
				curpuncnum = param;
				curpunc = punc_types[curpuncnum];
			}
			if (param >= punc_types_count)
				set_speakup_option(SPEAKUP_PUNCTUATION, curpuncnum);
		}
		if (options.respect_spd_defaults == 1 && safe_to_change_punc == 0) {
			safe_to_change_punc = 1;
		}
		break;

	case 'o':		/* set voice */
		switch (param) {
		case 0:
			LOG(5, "[Voice MALE1]");
			ret = spd_set_voice_type(conn, SPD_MALE1);
			break;
		case 1:
			LOG(5, "[Voice MALE2]");
			ret = spd_set_voice_type(conn, SPD_MALE2);
			break;
		case 2:
			LOG(5, "[Voice MALE3]");
			ret = spd_set_voice_type(conn, SPD_MALE3);
			break;
		case 3:
			LOG(5, "[Voice FEMALE1]");
			ret = spd_set_voice_type(conn, SPD_FEMALE1);
			break;
		case 4:
			LOG(5, "[Voice FEMALE2]");
			ret = spd_set_voice_type(conn, SPD_FEMALE2);
			break;
		case 5:
			LOG(5, "[Voice FEMALE3]");
			ret = spd_set_voice_type(conn, SPD_FEMALE3);
			break;
		case 6:
			LOG(5, "[Voice CHILD_MALE]");
			ret = spd_set_voice_type(conn, SPD_CHILD_MALE);
			break;
		case 7:
			LOG(5, "[Voice CHILD_FEMALE]");
			ret = spd_set_voice_type(conn, SPD_CHILD_FEMALE);
			break;
		default:
			LOG(1, "ERROR: Invalid voice %d!", param);
			break;
		}
		if (ret == -1)
			LOG(1, "ERROR: Can't set voice!");
		else {
			curvoicenum = param;
			curvoice = voice_types[curvoicenum];
		}
		if (param >= voice_types_count)
			set_speakup_option(SPEAKUP_VOICE, curvoicenum);
		break;

	case 'p':		/* set pitch command */
		if (pm)
			curpitch += pm;
		else
			curpitch = param;
		val = (curpitch * 20) + curpitchbase - 100;
		assert((val >= -100) && (val <= +100));
		LOG(5, "[pitch %d, param: %d]", val, param);
		ret = spd_set_voice_pitch(conn, val);
		if (ret == -1)
			LOG(1, "ERROR: Can't set pitch!");
		break;

	case 's':		/* speech rate */
		if (pm)
			currate += pm;
		else
			currate = param;
		if (options.respect_spd_defaults == 1)
			val = (currate * 20) + curratebase - 100;
		else
			val = (currate * 22) - 100;
		assert((val >= -100) && (val <= +100));
		LOG(5, "[rate %d, param: %d]", val, param);
		ret = spd_set_voice_rate(conn, val);
		if (ret == -1)
			LOG(1, "ERROR: Invalid rate!");
		break;

	case 'f':
		LOG(3, "WARNING: [frequency setting not supported,"
		    "use rate instead]");
		break;

	case 'v':
		if (pm)
			curvol += pm;
		else
			curvol = param;
		val = (curvol * 20) + curvolbase - 100;
		assert((val >= -100) && (val <= +100));
		LOG(5, "[vol %d, param: %d]", val, param);
		ret = spd_set_volume(conn, val);
		if (ret == -1)
			LOG(1, "ERROR: Invalid volume!");
		break;

	case 'x':
		LOG(3, "[tone setting not supported]");
		break;
	default:
		LOG(3, "ERROR: [%c: this command is not supported]", command);
	}
}

/* Say a single character.

UGLY HACK: Since currently this can either be a character
encountered while moving the cursor, a single character generated
by the application, I use the SSIP CHAR command for it. CHAR
generally gives more information and gives better result
for pressing keys. However, there might be some bad side-effects
in the situations while the characters are generated by reding
characters when moving with the cursor. It is currently impossible
to distinguish KEYs from CHARacters in Speakup.
*/
int say_single_character(char *character)
{
	int ret;
	char cmd[13];

	if (!strcmp(character, "\n"))
		return 0;

	LOG(5, "Saying single character: |%s|", character);

	/* It seems there is a bug in some versions of libspeechd
	   in function spd_say_char() */
	snprintf(cmd, 12, "CHAR %s", character);
	ret = spd_execute_command(conn, "SET SELF PRIORITY TEXT");
	if (ret != 0)
		return ret;
	ret = spd_execute_command(conn, cmd);
	if (ret != 0)
		return ret;

	return 0;
}

const char *ssml_less_than = "&lt;";
const char *ssml_greater_than = "&gt;";
const char *ssml_ampersand = "&amp;";
const char *ssml_single_quote = "&apos;";
const char *ssml_double_quote = "&quot;";

const char *ssml_entities[128];
int ssml_entity_lengths[128];

/*
  init_ssml_char_escapes: initialize the tables that describe SSML character
  escapes. */

void init_ssml_char_escapes(void)
{
	int i = 0;
	for (i = 0; i < (sizeof(ssml_entities) / sizeof(char *)); i++) {
		ssml_entity_lengths[i] = 0;
		ssml_entities[i] = NULL;
	}
	ssml_entities['<'] = ssml_less_than;
	ssml_entity_lengths['<'] = strlen(ssml_less_than);
	ssml_entities['>'] = ssml_greater_than;
	ssml_entity_lengths['>'] = strlen(ssml_greater_than);
	ssml_entities['&'] = ssml_ampersand;
	ssml_entity_lengths['&'] = strlen(ssml_ampersand);
	ssml_entities['\''] = ssml_single_quote;
	ssml_entity_lengths['\''] = strlen(ssml_single_quote);
	ssml_entities['\"'] = ssml_double_quote;
	ssml_entity_lengths['\"'] = strlen(ssml_double_quote);
}

char *recode_text(char *text)
{
	iconv_t cd;
	size_t in_bytes, out_bytes, enc_bytes;
	char *utf8_text, *out_p;

	utf8_text = malloc(4 * strlen(text) + 1);
	if (utf8_text == NULL) {
		LOG(1, "ERROR: Charset conversion failed, reason: %s",
		    strerror(errno));
		return NULL;
	}

	out_p = utf8_text;
	out_bytes = 4 * strlen(text);
	in_bytes = strlen(text);

	/* Initialize ICONV for charset conversion */
	cd = iconv_open("utf-8", options.speakup_coding);
	if (cd == (iconv_t) - 1)
		FATAL(1, "Requested character set conversion not possible"
		      "by iconv: %s!", strerror(errno));

	enc_bytes = iconv(cd, &text, &in_bytes, &out_p, &out_bytes);
	if (enc_bytes == -1) {
		LOG(1, "ERROR: Charset conversion failed, reason: %s",
		    strerror(errno));
		utf8_text = NULL;
	} else {
		*out_p = 0;
		LOG(5, "Recoded text: |%s|", utf8_text);
	}

	iconv_close(cd);

	return utf8_text;
}

/*
  speak_string: send a string containing more than one printable character 
  to Speech Dispatcher.  */

int speak_string(char *text)
{
	char *utf8_text = NULL;
	char *ssml_text = NULL;
	int ret = 0;
	utf8_text = recode_text(text);
	if (utf8_text == NULL)
		ret = -1;
	else {
		int bufsize = strlen(utf8_text) + 16;
		ssml_text = malloc(bufsize);
		if (ssml_text == NULL)
			ret = -1;
		else {
			snprintf(ssml_text, bufsize,
				 "<speak>%s</speak>", utf8_text);
			LOG(5, "Sending to speechd as text: |%s|", ssml_text);
			ret = spd_say(conn, SPD_MESSAGE, ssml_text);
		}
	}
	xfree(utf8_text);
	xfree(ssml_text);
	return ret;
}

int speak(char *text)
{
	/* Check whether text contains more than one
	   printable character. If so, use spd_say,
	   otherwise use say_single_character.

	   Returns 0 on success, -2 on speechd communication
	   errors, -1 on other errors.
	 */

	int printables = 0;
	int i;
	char *utf8_text = NULL;
	int spd_ret = 0, ret = 0;
	char character[2];

	assert(text);
	for (i = 0; i <= strlen(text) - 1; i++) {
		if (!isspace(text[i])) {
			if (printables == 0)
				sprintf(character, "%c", text[i]);
			printables++;
		}
	}

	LOG(5, "Text before recoding: |%s|", text);

	if (printables == 1 && !isupper(*character)) {
		utf8_text = recode_text(character);
		LOG(5, "Sending to speechd as character: |%s|", utf8_text);
		spd_ret = say_single_character(utf8_text);
	} else if (printables >= 1) {
		spd_ret = speak_string(text);
	}
	/* Else printables is 0, nothing to do. */

	if (spd_ret != 0)
		ret = -2;
	xfree(utf8_text);
	return ret;
}

int parse_buf(char *buf, size_t bytes)
{
	char helper[20];
	char cmd_type = ' ';
	int n, m;
	int current_char;
	unsigned int param;
	int pm;

	int i;
	//char *mark_tag="<mark name=\"%s\">";
	char *pi, *po;
	static char text[BUF_SIZE * 16];	/* Definitely big enough. */

	assert(bytes <= BUF_SIZE);

	param = 0;
	pi = buf;
	po = text;
	m = 0;

	for (i = 0; i < bytes; i++) {
		/* Stop speaking */
		if (buf[i] == DTLK_STOP) {
			spd_cancel(conn);
			LOG(5, "[stop]");
			pi = &(buf[i + 1]);
			po = text;
			m = 0;
		}

		/* Process a command */
		else if (buf[i] == DTLK_CMD) {
			/* If the digit is signed integer, read the sign.  We have
			   to do it this way because +3, -3 and 3 seem to be three
			   different things in this protocol */
			i++;
			if (buf[i] == '+')
				i++, pm = 1;
			else if (buf[i] == '-')
				i++, pm = -1;
			else
				pm = 0;	/* No sign */

			/* Read the numerical parameter (one or more digits) */
			n = 0;
			while (isdigit(buf[i]) && n < 15)
				helper[n++] = buf[i++];
			if (n) {
				helper[n] = 0;
				param = strtol(helper, NULL, 10);
				cmd_type = buf[i];
			}

			if (cmd_type == 'i') {
				LOG(5, "Insert Index %d", param);
				sprintf(helper, "<mark name=\"%d\"/>", param);
				strcpy(po, helper);
				pi = &(buf[i + 1]);
				po += strlen(helper) * sizeof(char);
				*po = 0;
			} else {
				/* If there is some text before this command, say it */
				if (m > 0) {
					*po = '\0';
					LOG(5, "text: |%s|", text);
					LOG(5, "[speaking (2)]");
					speak(text);
					m = 0;
				}
				/* Now when we have the command (cmd_type) and it's
				   parameter, let's communicate it to speechd */
				process_command(cmd_type, param, pm);
				pi = &(buf[i + 1]);
				po = text;
			}
		} else {
			/* This is ordinary text, so put the byte into our text
			   buffer for later synthesis. */
			m++;
			current_char = *pi;
			if (isascii(current_char)
			    && (ssml_entities[current_char] != NULL)) {
				strcpy(po, ssml_entities[current_char]);
				po += ssml_entity_lengths[current_char];
			} else {
				*po = *pi;
				po += sizeof(char);
			}
			pi += sizeof(char);
		}
	}
	*po = 0;

	/* Finally, say the text we read from /dev/softsynth */
	assert(m >= 0);

	if (m != 0) {
		LOG(5, "text: |%s %d|", text, m);
		LOG(5, "[speaking]");
		speak(text);
		LOG(5, "---");
	}

	return 0;
}

void spd_spk_terminate(int sig)
{
	LOG(1, "Terminating...");
	/* TODO: Resolve race  */
	speechd_close();
	close(fd);
	fclose(logfile);
	exit(1);
}

void spd_spk_reset(int sig)
{
	speechd_close();
	speechd_init();
}

int create_pid_file()
{
	FILE *pid_file;
	int pid_fd;
	struct flock lock;
	int ret;

	/* If the file exists, examine it's lock */
	pid_file = fopen(spd_spk_pid_file, "r");
	if (pid_file != NULL) {
		pid_fd = fileno(pid_file);

		lock.l_type = F_WRLCK;
		lock.l_whence = SEEK_SET;
		lock.l_start = 1;
		lock.l_len = 3;

		/* If there is a lock, exit, otherwise remove the old file */
		ret = fcntl(pid_fd, F_GETLK, &lock);
		if (ret == -1) {
			fprintf(stderr,
				"Can't check lock status of an existing pid file.\n");
			return -1;
		}

		fclose(pid_file);
		if (lock.l_type != F_UNLCK) {
			fprintf(stderr, "Speechd-Up already running.\n");
			return -1;
		}

		unlink(spd_spk_pid_file);
	}

	/* Create a new pid file and lock it */
	pid_file = fopen(spd_spk_pid_file, "w");
	if (pid_file == NULL) {
		fprintf(stderr,
			"Can't create pid file in %s, wrong permissions?\n",
			spd_spk_pid_file);
		return -1;
	}
	fprintf(pid_file, "%d\n", getpid());
	fflush(pid_file);

	pid_fd = fileno(pid_file);
	lock.l_type = F_WRLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = 1;
	lock.l_len = 3;

	ret = fcntl(pid_fd, F_SETLK, &lock);
	if (ret == -1) {
		fprintf(stderr, "Can't set lock on pid file.\n");
		return -1;
	}

	return 0;
}

void destroy_pid_file()
{
	unlink(spd_spk_pid_file);
}

int main(int argc, char *argv[])
{
	size_t chars_read;
	char buf[BUF_SIZE + 1];
	int ret;

	options_set_default();
	options_parse(argc, argv);

	if (!strcmp(PIDPATH, ""))
		spd_spk_pid_file = (char *)strdup("/var/run/speechd-up.pid");
	else
		spd_spk_pid_file = (char *)strdup(PIDPATH "/speechd-up.pid");

	if (create_pid_file() == -1)
		exit(1);

	logfile = stdout;
	load_configuration();

	logfile = fopen(options.log_file_name, "w+");
	if (logfile == NULL) {
		fprintf(stderr,
			"ERROR: Can't open logfile in %s! Wrong permissions?\n",
			options.log_file_name);
		exit(1);
	}

	/* Fork, set uid, chdir, etc. */
	if (options.spd_spk_mode == MODE_DAEMON) {
		if (daemon(0, 0))
			return 1;
		/* Re-create the pid file under this process */
		destroy_pid_file();
		if (create_pid_file() == -1)
			return 1;
	}

	/* Register signals */
	(void)signal(SIGINT, spd_spk_terminate);
	(void)signal(SIGHUP, spd_spk_reset);

	LOG(1, "Speechd-speakup starts!");

	if (!options.probe_mode) {
		if ((fd = open(options.speakup_device, O_RDWR)) < 0) {
			LOG(1,
			    "Error while openning the device in read/write mode %d,%s",
			    errno, strerror(errno));
			LOG(1, "Trying to open the device in the old way.");
			if ((fd = open(options.speakup_device, O_RDONLY)) < 0) {
				LOG(1,
				    "Error while openning the device in read mode %d,%s",
				    errno, strerror(errno));
				FATAL(2,
				      "ERROR! Unable to open soft synth device (%s)\n",
				      options.speakup_device);
				return -1;
			} else {
				LOG(1,
				    "It seems you are using an older version of Speakup "
				    "that doesn't support index marking. This is not a problem "
				    "but some more advanced functions of Speakup might not work "
				    "until you upgrade Speakup.");
			}

		}
		if (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK) == -1) {
			FATAL(5, "fcntl() failed");
			return (-1);
		}
	}

	speechd_init();

	if (options.probe_mode) {
		LOG(1,
		    "This is just a probe mode. Not trying to read Speakup's device.\n");
		LOG(1, "Trying to say something on Speech Dispatcher\n");
		spd_say(conn, SPD_MESSAGE,
			"Hello! It seems SpeechD-Up works correctly!\n");
		LOG(1, "Trying to close connection to Speech Dispatcher\n");
		spd_close(conn);
		LOG(1, "SpeechD-Up is terminating correctly in probe mode");
		return 0;
	}

	ret = spd_set_data_mode(conn, SPD_DATA_SSML);
	if (ret) {
		LOG(1,
		    "ERROR: This version of Speech Dispatcher doesn't support SSML mode.\n"
		    "Please use a newer version of Speech Dispatcher (at least 0.5)");
		FATAL(6, "SSML not supported in Speech Dispatcher");
	}

	if (!options.dont_init_tables) {
		ret = init_speakup_tables();
		if (ret) {
			LOG(1,
			    "ERROR: It was not possible to init Speakup /proc tables for\n"
			    "characters and character types."
			    "This error might appear because you use an old version of Speakup!"
			    "If your instalation of Speakup is new:In order for internationalization\n"
			    "or correct Speech Dispatcher support (like sound icons) to be\n"
			    "working, you need to set each entry in /proc/speakup/characters\n"
			    "except for space to its value and each entry in /proc/speakup/chartab"
			    "which represents a valid speakable character to ALPHA.\n");
		}
	}

	while (1) {
		FD_ZERO(&fd_list);
		FD_SET(fd, &fd_list);
		if (select(fd + 1, &fd_list, NULL, NULL, NULL) < 0) {
			if (errno == EINTR)
				continue;
			FATAL(5, "select() failed");
			close(fd);
			return -1;
		}
		chars_read = read(fd, buf, BUF_SIZE);
		if (chars_read < 0) {
			FATAL(5, "read() failed");
			close(fd);
			return -1;
		}
		buf[chars_read] = 0;
		LOG(5, "Main loop characters read = %d : (%s)", chars_read,
		    buf);
		parse_buf(buf, chars_read);
	}

	return 0;
}
