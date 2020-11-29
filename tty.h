// Virtual Choir Rehearsal Room  Copyright (C) 2020  Lukas Ondracek <ondracek.lukas@gmail.com>, use under GNU GPLv3

// #define ttyPromptF(sfmt, var, ...) {printf(__VA_ARGS__); scanf(sfmt, &var); }

#ifdef __WIN32__
#include <windows.h>
#else
#include <unistd.h>
#include <termios.h>
#endif

#ifdef __WIN32__
HANDLE ttyStdinHandle, ttyStdoutHandle;
#endif

void ttyInit() {
#ifdef __WIN32__
	ttyStdinHandle = GetStdHandle(STD_INPUT_HANDLE);
	ttyStdoutHandle = GetStdHandle(STD_OUTPUT_HANDLE);
	CONSOLE_CURSOR_INFO info;
	info.dwSize = 100;
	info.bVisible = FALSE;
	SetConsoleCursorInfo(ttyStdoutHandle, &info);
#endif
}

void ttyMoveUp(int lines) {
#ifdef __WIN32__
	fflush(stdout);
	CONSOLE_SCREEN_BUFFER_INFO screenInfo;
	GetConsoleScreenBufferInfo(ttyStdoutHandle, &screenInfo);
	screenInfo.dwCursorPosition.Y -= lines;
	SetConsoleCursorPosition(ttyStdoutHandle, screenInfo.dwCursorPosition);
#else
	while (lines-- > 0) {
		printf("\033[A");
	}
#endif
}

void ttyClearStatus();
int ttyReadKey() {
#ifdef __WIN32__
	DWORD tty_opts_backup;
	GetConsoleMode(ttyStdinHandle, &tty_opts_backup);
	DWORD tty_opts_raw = 0;
	SetConsoleMode(ttyStdinHandle, tty_opts_raw);
#else
	struct termios tty_opts_backup, tty_opts_raw;

	tcgetattr(STDIN_FILENO, &tty_opts_backup);

	tty_opts_raw = tty_opts_backup;
	cfmakeraw(&tty_opts_raw);
	tty_opts_raw.c_oflag = tty_opts_backup.c_oflag;
	//tty_opts_raw.c_lflag |= ISIG | VINTR | VQUIT | VSUSP;

	tcsetattr(STDIN_FILENO, TCSANOW, &tty_opts_raw);
#endif

	// Read and print characters from stdin
	int c = getchar();
	// Restore previous TTY settings
#ifdef __WIN32__
	//if (c == '\r') c = getchar();
	SetConsoleMode(ttyStdinHandle, tty_opts_backup);
#else
	tcsetattr(STDIN_FILENO, TCSANOW, &tty_opts_backup);
#endif
	if (c == 3) {
		ttyClearStatus();
		printf("\n");
		fflush(stdout);
		exit(130);
	}
	return c;
}

int ttyPromptKey(char *prompt, char *allowedKeys) {
	printf("%s: ", prompt);
	fflush(stdout);
	int c;
	char *s;
	do {
		c = ttyReadKey();
		for (s = allowedKeys; (*s != '\0') && (*s != c); s++);
	} while ((c != EOF) && (c != *s));

	if (c > 31) {
		printf("%c\n", c);
	} else {
		printf("\n");
	}
	return c;
}

char *ttyPromptStr(char *prompt) {
	static size_t strLen = 1;
	static char *str = NULL;
	if (!str) str = malloc(64);
	printf("%s: ", prompt);
	int c;
	size_t i = 0;
	while (true) {
		c = getchar();
		if ((c == EOF) || (c == '\n')) break;
		if (c == '\r') continue;
		if ((i+1) >= strLen) {
			str = realloc(str, strLen *= 2);
		}
		str[i++] = c;
	}
	str[i] = '\0';

	return str;
}
void ttyDiscardLineEnd() {
	int c;
	do {
		c = getchar();
	} while ((c != '\n') && (c != EOF));
}

int ttyPromptInt(char *prompt) {
	printf("%s: ", prompt);
	int ret;
	scanf("%d", &ret);
	ttyDiscardLineEnd();
	return ret;
}

double ttyPromptDouble(char *prompt) {
	printf("%s: ", prompt);
	double ret;
	scanf("%lf", &ret);
	ttyDiscardLineEnd();
	return ret;
}

float ttyPromptFloat(char *prompt) {
	printf("%s: ", prompt);
	float ret;
	scanf("%f", &ret);
	ttyDiscardLineEnd();
	return ret;
}


char ttyStatusStr[STATUS_HEIGHT * (STATUS_WIDTH + 1) + 1];
int ttyStatusLines = 0;
int ttyPrevStatusLines = 0;
void ttyUpdateStatus(char *s, int firstLine) {
	int i, l = firstLine;
	char *s2;
	if (ttyStatusLines < firstLine) {
		s2 = ttyStatusStr + ttyStatusLines * (STATUS_WIDTH + 1);
		for (; ttyStatusLines < firstLine; ttyStatusLines++) {
			for (i = 0; i < STATUS_WIDTH; i++) *s2++ = ' ';
			*s2++ = '\n';
		}
	} else {
		s2 = ttyStatusStr + l * (STATUS_WIDTH + 1);
	}
	i = 0;

	while (true) {
		if ((*s == '\0') || (*s == '\n')) {
			for (; i < STATUS_WIDTH; i++) *s2++ = ' ';
			*s2++ = '\n'; l++; i=0;
			if (*s == '\0') break;
			s++; continue;
		}
		*s2++ = *s++; i++;
		if (i >= STATUS_WIDTH) {
			while (*s && (*s != '\n')) s++;
		}
	}
	if (ttyStatusLines <= l) {
		ttyStatusLines = l;
	}
}
void ttyResetStatus() {
	ttyStatusLines = 0;
	ttyStatusStr[0] = '\0';
}

void ttyPrintStatus() {
	{
		int tmp = ttyStatusLines;
		if (ttyPrevStatusLines > ttyStatusLines) {
			ttyUpdateStatus("", ttyPrevStatusLines - 1);
		}
		ttyPrevStatusLines = tmp;
	}
	ttyStatusStr[ttyStatusLines * (STATUS_WIDTH + 1) - 1] = '\0';
	puts(ttyStatusStr);
	ttyMoveUp(ttyStatusLines);
}

void ttyClearStatus() {
	ttyStatusLines = 0;
	ttyPrintStatus();
}

void ttyFormatSndLevel(char **s, float dBAvg, float dBPeak) {
	*(*s)++ = '[';
	for (int i = 38; i > 0; i--) {
		float db = -i * 2;
		*(*s)++ = dBAvg > db ? '#' : dBPeak > db ? '+' : '-';
	}
	*(*s)++ = ']';
	if ((dBAvg > -1000) && (dBPeak > -100)) {
		*s += sprintf(*s, "%4.0f dB (%3.0f dB)", dBAvg, dBPeak);
	} else {
		*s += sprintf(*s, " silent");
	}

	// [########++++++------------------------] -xx dB (-xx dB)
}
