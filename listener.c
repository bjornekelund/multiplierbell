/*
 * dxlog_mult_listener.c
 *
 * Listens for DXLog UDP datagrams on port 9888.
 * Plays a beep/sound whenever mult1, mult2, or mult3 is non-empty.
 *
 * Sound options (choose ONE by setting SOUND_MODE below):
 *
 *   SOUND_MODE_WAV   — play a WAV file via aplay
 *   SOUND_MODE_BEEP  — synthesise a tone in memory and play it via aplay
 *   SOUND_MODE_ALSA  — synthesise a tone directly through ALSA (no aplay,
 *                       requires libasound2-dev)
 *
 * Build (WAV or BEEP mode — no extra libs):
 *   gcc -O2 -Wall -o dxlog_mult_listener dxlog_mult_listener.c -lm
 *
 * Build (ALSA mode):
 *   gcc -O2 -Wall -o dxlog_mult_listener dxlog_mult_listener.c -lm -lasound
 *
 * Run:
 *   ./dxlog_mult_listener
 *
 * Raspberry Pi OS (Bullseye / Bookworm), Raspberry Pi 4.
 * Make sure audio output is configured:
 *   sudo raspi-config  →  System Options → Audio
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* ------------------------------------------------------------------ */
/*  Sound mode — pick exactly one                                       */
/* ------------------------------------------------------------------ */
#define SOUND_MODE_WAV   0   /* play a WAV file with aplay             */
#define SOUND_MODE_BEEP  1   /* generate a tone, pipe it to aplay      */
#define SOUND_MODE_ALSA  2   /* generate a tone via ALSA directly       */

#define SOUND_MODE SOUND_MODE_WAV   /* ← change this to suit         */

/* ------------------------------------------------------------------ */
/*  Configuration                                                        */
/* ------------------------------------------------------------------ */
#define LISTEN_PORT   12060

/* Used only in SOUND_MODE_WAV: */
#define WAV_FILE      "./handbell.wav"

/* Used in SOUND_MODE_BEEP and SOUND_MODE_ALSA: */
#define BEEP_FREQ_HZ    880     /* tone frequency  (Hz)               */
#define BEEP_DURATION   400     /* tone duration   (ms)               */
#define BEEP_VOLUME     0.6     /* 0.0 – 1.0                          */

/* ALSA device (SOUND_MODE_ALSA only). "default" usually works.
   Use "plughw:0,0" to target the Pi's built-in audio. */
#define ALSA_DEVICE   "default"

/* ------------------------------------------------------------------ */
/*  ALSA headers (only compiled when SOUND_MODE == SOUND_MODE_ALSA)    */
/* ------------------------------------------------------------------ */
#if SOUND_MODE == SOUND_MODE_ALSA
#include <alsa/asoundlib.h>
#endif

/* ================================================================== */
/*  Simple XML field extractor (case-insensitive tag matching)         */
/*                                                                      */
/*  Finds <tag>value</tag> regardless of the capitalisation used in    */
/*  the XML.  Returns 1 on success, 0 if the tag is not found.        */
/* ================================================================== */
static int xml_get_field(const char *xml, const char *tag,
                         char *buf, size_t buflen)
{
    char open_tag[64], close_tag[64];
    snprintf(open_tag,  sizeof(open_tag),  "<%s>",  tag);
    snprintf(close_tag, sizeof(close_tag), "</%s>", tag);

    size_t otlen = strlen(open_tag);
    size_t ctlen = strlen(close_tag);

    /* Case-insensitive search for opening tag */
    const char *start = NULL;
    for (const char *p = xml; *p; p++) {
        if (strncasecmp(p, open_tag, otlen) == 0) {
            start = p + otlen;
            break;
        }
    }
    if (!start) return 0;

    /* Case-insensitive search for closing tag */
    const char *end = NULL;
    for (const char *p = start; *p; p++) {
        if (strncasecmp(p, close_tag, ctlen) == 0) {
            end = p;
            break;
        }
    }
    if (!end) return 0;

    size_t len = (size_t)(end - start);
    if (len >= buflen) len = buflen - 1;
    memcpy(buf, start, len);
    buf[len] = '\0';

    /* Trim leading whitespace */
    char *p = buf;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (p != buf) memmove(buf, p, strlen(p) + 1);

    /* Trim trailing whitespace */
    char *e = buf + strlen(buf) - 1;
    while (e >= buf && (*e == ' ' || *e == '\t' || *e == '\r' || *e == '\n'))
        *e-- = '\0';

    return 1;
}

/* ================================================================== */
/*  Sound implementations                                               */
/* ================================================================== */

/* ---- WAV file via aplay ------------------------------------------ */
#if SOUND_MODE == SOUND_MODE_WAV
static void play_sound(void)
{
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "aplay -q '%s' &", WAV_FILE);
    if (system(cmd) != 0)
        fprintf(stderr, "Warning: aplay returned error\n");
}
#endif

/* ---- Generate tone, pipe raw PCM to aplay ------------------------- */
#if SOUND_MODE == SOUND_MODE_BEEP

#define SAMPLE_RATE 44100

static void play_sound(void)
{
    int num_samples = (int)((SAMPLE_RATE * BEEP_DURATION) / 1000);
    short *samples = malloc((size_t)num_samples * sizeof(short));
    if (!samples) { perror("malloc"); return; }

    for (int i = 0; i < num_samples; i++) {
        /* Sine wave with a short linear fade-in/out to avoid clicks */
        double t      = (double)i / SAMPLE_RATE;
        double fade   = 1.0;
        int    fadelen = SAMPLE_RATE / 50;   /* 20 ms */
        if (i < fadelen)
            fade = (double)i / fadelen;
        else if (i > num_samples - fadelen)
            fade = (double)(num_samples - i) / fadelen;

        double s  = BEEP_VOLUME * fade * sin(2.0 * M_PI * BEEP_FREQ_HZ * t);
        samples[i] = (short)(s * 32767.0);
    }

    /*
     * Pipe raw signed 16-bit little-endian mono 44100 Hz PCM to aplay.
     * aplay -t raw -f S16_LE -r 44100 -c 1
     */
    FILE *p = popen("aplay -q -t raw -f S16_LE -r 44100 -c 1 2>/dev/null", "w");
    if (!p) {
        perror("popen aplay");
        free(samples);
        return;
    }
    fwrite(samples, sizeof(short), (size_t)num_samples, p);
    pclose(p);   /* waits for aplay to finish */
    free(samples);
}
#endif

/* ---- ALSA direct -------------------------------------------------- */
#if SOUND_MODE == SOUND_MODE_ALSA

#define SAMPLE_RATE 44100

static void play_sound(void)
{
    snd_pcm_t *handle;
    int rc;

    rc = snd_pcm_open(&handle, ALSA_DEVICE, SND_PCM_STREAM_PLAYBACK, 0);
    if (rc < 0) {
        fprintf(stderr, "ALSA open error: %s\n", snd_strerror(rc));
        return;
    }

    snd_pcm_hw_params_t *params;
    snd_pcm_hw_params_alloca(&params);
    snd_pcm_hw_params_any(handle, params);
    snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(handle, params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(handle, params, 1);

    unsigned int rate = SAMPLE_RATE;
    snd_pcm_hw_params_set_rate_near(handle, params, &rate, 0);
    snd_pcm_hw_params(handle, params);

    int num_samples = (SAMPLE_RATE * BEEP_DURATION) / 1000;
    short *samples  = malloc((size_t)num_samples * sizeof(short));
    if (!samples) { perror("malloc"); snd_pcm_close(handle); return; }

    for (int i = 0; i < num_samples; i++) {
        double t     = (double)i / SAMPLE_RATE;
        double fade  = 1.0;
        int fadelen  = SAMPLE_RATE / 50;
        if (i < fadelen)
            fade = (double)i / fadelen;
        else if (i > num_samples - fadelen)
            fade = (double)(num_samples - i) / fadelen;

        double s  = BEEP_VOLUME * fade * sin(2.0 * M_PI * BEEP_FREQ_HZ * t);
        samples[i] = (short)(s * 32767.0);
    }

    snd_pcm_writei(handle, samples, (snd_pcm_uframes_t)num_samples);
    snd_pcm_drain(handle);
    snd_pcm_close(handle);
    free(samples);
}
#endif

/* ================================================================== */
/*  Timestamp helper                                                     */
/* ================================================================== */
static void print_timestamp(void)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", t);
    printf("[%s] ", buf);
}

/* ================================================================== */
/*  Process one UDP datagram                                            */
/* ================================================================== */
static void process_datagram(const char *buf, size_t len,
                              const struct sockaddr_in *src)
{
    /* Ignore datagrams that do not contain <contactinfo> (case-insensitive) */
    int found_ci = 0;
    for (size_t i = 0; i + 13 <= len; i++) {
        if (strncasecmp(buf + i, "<contactinfo>", 13) == 0) {
            found_ci = 1;
            break;
        }
    }
    if (!found_ci) return;
/*    printf("buf=");
    for (int i = 0; i <= len; i++)
      printf("%c", *(buf + i));
    printf("\n"); */


    /* NUL-terminate */
    char *xml = malloc(len + 1);
    if (!xml) { perror("malloc"); return; }
    memcpy(xml, buf, len);
    xml[len] = '\0';

    char call[64]  = "";
    char band[32]  = "";
    char mode[16]  = "";
    char mult1[64] = "";
    char mult2[64] = "";
    char mult3[64] = "";

    char newqso[16] = "";
    char xqso[16]   = "";

    xml_get_field(xml, "call",   call,   sizeof(call));
    xml_get_field(xml, "band",   band,   sizeof(band));
    xml_get_field(xml, "mode",   mode,   sizeof(mode));
    xml_get_field(xml, "mult1",  mult1,  sizeof(mult1));
    xml_get_field(xml, "mult2",  mult2,  sizeof(mult2));
    xml_get_field(xml, "mult3",  mult3,  sizeof(mult3));
    xml_get_field(xml, "newqso", newqso, sizeof(newqso));
    xml_get_field(xml, "xqso",   xqso,   sizeof(xqso));

    /* ---- Trigger: all three conditions must be true ---------------- */
    int has_mult = (mult1[0] != '\0') ||
                   (mult2[0] != '\0') ||
                   (mult3[0] != '\0');
    int is_new   = (strcasecmp(newqso, "true")  == 0);

    print_timestamp();
    printf("PKT from %-15s call=%-8s band=%-3s mode=%-3s mult1=%-2s  mult2=%-2s  mult3=%-2s newqso=%-5s",
           inet_ntoa(src->sin_addr),
           call[0]   ? call   : "-",
           band[0]   ? band   : "-",
           mode[0]   ? mode   : "-",
           mult1[0]  ? mult1  : "-",
           mult2[0]  ? mult2  : "-",
           mult3[0]  ? mult3  : "-",
           newqso[0] ? newqso : "-");

    if (has_mult && is_new) {
        printf("  *** MULT → SOUND ***");
        fflush(stdout);
        play_sound();
    }
    printf("\n");
    fflush(stdout);

    free(xml);
}

/* ================================================================== */
/*  Main                                                                 */
/* ================================================================== */
int main(void)
{
    const char *mode_name =
#if   SOUND_MODE == SOUND_MODE_WAV
        "WAV file via aplay (" WAV_FILE ")";
#elif SOUND_MODE == SOUND_MODE_BEEP
        "synthesised tone via aplay (no file needed)";
#else
        "synthesised tone via ALSA direct";
#endif

    printf("=== DXLog Multiplier Listener ===\n");
    printf("Port      : UDP %d\n", LISTEN_PORT);
    printf("Trigger   : mult1/mult2/mult3 non-empty AND newqso=true\n");
    printf("Sound     : %s\n", mode_name);
#if SOUND_MODE == SOUND_MODE_BEEP || SOUND_MODE == SOUND_MODE_ALSA
    printf("Tone      : %d Hz, %d ms, volume %.0f%%\n",
           BEEP_FREQ_HZ, BEEP_DURATION, BEEP_VOLUME * 100.0);
#endif
    printf("\n");
    fflush(stdout);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(LISTEN_PORT);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sock);
        return 1;
    }

    printf("Listening on 0.0.0.0:%d …\n\n", LISTEN_PORT);
    fflush(stdout);

    static char buf[65536];
    for (;;) {
        struct sockaddr_in src;
        socklen_t srclen = sizeof(src);
        ssize_t n = recvfrom(sock, buf, sizeof(buf) - 1, 0,
                             (struct sockaddr *)&src, &srclen);
        if (n < 0) { perror("recvfrom"); continue; }
        process_datagram(buf, (size_t)n, &src);
    }

    close(sock);
    return 0;
}
