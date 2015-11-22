/* Feel free to use this example code in any way
   you see fit (Public Domain) */

#include <sys/types.h>
#ifndef _WIN32
#include <sys/select.h>
#include <sys/socket.h>
#else
#include <winsock2.h>
#endif
#include <microhttpd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>

#include <espeak/speak_lib.h>

#define POSTBUFFERSIZE  4096 
#define MAXTEXTSIZE     4096
#define MAXVOICESIZE    20
#define MAXANSWERSIZE   4096

#define GET             0
#define POST            1

struct connection_info_struct
{
  int connectiontype;
  char *textstring;
  char *voicestring;
  char *answerstring;
  struct MHD_PostProcessor *postprocessor;
};

const char *askpage = "<html><body>\
                       Text to convert:<br>\
                       <form action=\"/ipa\" method=\"post\">\
                       <input name=\"text\" type=\"text\">\
                       <input name=\"voice\" type=\"hidden\" value=\"en\">\
                       <input type=\"submit\" value=\" Send \"></form>\
                       </body></html>";

const char *greetingpage =
  "<html><body><h1>Welcome, %s!</center></h1></body></html>";

const char *errorpage =
  "<html><body>This doesn't seem to be right.</body></html>";


static int 
SynthCallback(short *wav, int numsamples, espeak_EVENT *events)
{
  return 0;
}

char*
getIpa(char *in) 
{
  int option_phonemes = 3;
  int option_mbrola_phonemes = 0;
  char* buf;
  buf=(char *) malloc(MAXTEXTSIZE*sizeof(char));
  FILE *f_phonemes_out = fmemopen(buf, MAXTEXTSIZE, "w");
    if (f_phonemes_out == NULL)
	printf("ERROR f_phonemes_out is null\n");
 
  espeak_SetPhonemeTrace(option_phonemes | option_mbrola_phonemes,f_phonemes_out);
  espeak_SetSynthCallback(SynthCallback);
  int synth_flags = espeakCHARS_AUTO | espeakPHONEMES | espeakENDPAUSE;
  espeak_Synth(in, strlen(in) + 1, 0, POS_CHARACTER, 0, synth_flags, NULL, NULL);
  fclose(f_phonemes_out);
  //printf(">%s<\n", buf);
  return buf;
}

int
checkForSignals(void)
{
    sigset_t pending;
    sigpending(&pending);
    if (sigismember(&pending, SIGTERM)) {
        // Handle SIGTERM...
        return 1;
    }
    return 0;
}

static int
send_result (struct MHD_Connection *connection, const char *page)
{
  int ret;
  struct MHD_Response *response;


  response =
    MHD_create_response_from_buffer (strlen (page), (void *) page,
				     MHD_RESPMEM_PERSISTENT);
  if (!response)
    return MHD_NO;
  MHD_add_response_header (response, "Content-Type", "text/plain; charset=utf-8");
  ret = MHD_queue_response (connection, MHD_HTTP_OK, response);
  MHD_destroy_response (response);

  return ret;
}

static int
send_page (struct MHD_Connection *connection, const char *page)
{
  int ret;
  struct MHD_Response *response;


  response =
    MHD_create_response_from_buffer (strlen (page), (void *) page,
				     MHD_RESPMEM_PERSISTENT);
  if (!response)
    return MHD_NO;
  ret = MHD_queue_response (connection, MHD_HTTP_OK, response);
  MHD_destroy_response (response);

  return ret;
}

static int
send_error_page (struct MHD_Connection *connection, const char *page)
{
  int ret;
  struct MHD_Response *response;


  response =
    MHD_create_response_from_buffer (strlen (page), (void *) page,
				     MHD_RESPMEM_PERSISTENT);
  if (!response)
    return MHD_NO;
  ret = MHD_queue_response (connection, MHD_HTTP_INTERNAL_SERVER_ERROR, response);
  MHD_destroy_response (response);

  return ret;
}

static int
iterate_post (void *coninfo_cls, enum MHD_ValueKind kind, const char *key,
              const char *filename, const char *content_type,
              const char *transfer_encoding, const char *data, uint64_t off,
              size_t size)
{
  struct connection_info_struct *con_info = coninfo_cls;
  //printf("inside iterate_post %s\n", key);
  if (0 == strcmp (key, "text"))
    {
      if ((size > 0) && (size <= MAXTEXTSIZE))
        {
          char *textstring;
          textstring = malloc (MAXTEXTSIZE);
          if (!textstring)
            return MHD_NO;
          snprintf (textstring, MAXTEXTSIZE, "%s", data);
          con_info->textstring = textstring;
        }
      else
        con_info->textstring = NULL;

    }
    else if (0 == strcmp (key, "voice"))
    {
      if ((size > 0) && (size <= MAXVOICESIZE))
        {
          char *voicestring;
          voicestring = malloc (MAXVOICESIZE);
          if (!voicestring)
            return MHD_NO;
	  snprintf(voicestring, MAXVOICESIZE, "%s", data);
          con_info->voicestring = voicestring;
        }
      else
        con_info->voicestring = NULL;

    }
    if (NULL != con_info->voicestring && NULL != con_info->textstring)
    {
       //printf("Got both: %s, %s\n", con_info->voicestring, con_info->textstring);
       con_info->answerstring = getIpa(con_info->textstring);
       
       return MHD_NO;
    }
    else 
    {
       return MHD_YES;
    }
}

static void
request_completed (void *cls, struct MHD_Connection *connection,
                   void **con_cls, enum MHD_RequestTerminationCode toe)
{
  struct connection_info_struct *con_info = *con_cls;

  if (NULL == con_info)
    return;

  if (con_info->connectiontype == POST)
    {
      MHD_destroy_post_processor (con_info->postprocessor);
      if (con_info->textstring)
        free (con_info->textstring);
      if (con_info->voicestring)
        free (con_info->voicestring);
      if (con_info->answerstring)
        free (con_info->answerstring);
    }

  free (con_info);
  *con_cls = NULL;
}


static int
answer_to_connection (void *cls, struct MHD_Connection *connection,
                      const char *url, const char *method,
                      const char *version, const char *upload_data,
                      size_t *upload_data_size, void **con_cls)
{
  if (NULL == *con_cls)
    {
      struct connection_info_struct *con_info;

      con_info = malloc (sizeof (struct connection_info_struct));
      if (NULL == con_info)
        return MHD_NO;
      con_info->textstring = NULL;
      con_info->voicestring = NULL;
      con_info->answerstring = NULL;

      if (0 == strcmp (method, "POST"))
        {
          con_info->postprocessor =
            MHD_create_post_processor (connection, POSTBUFFERSIZE,
                                       iterate_post, (void *) con_info);

          if (NULL == con_info->postprocessor)
            {
              free (con_info);
              return MHD_NO;
            }

          con_info->connectiontype = POST;
        }
      else
        con_info->connectiontype = GET;

      *con_cls = (void *) con_info;

      return MHD_YES;
    }

  if (0 == strcmp (method, "GET"))
    {
      return send_page (connection, askpage);
    }

  if (0 == strcmp (method, "POST"))
    {
      struct connection_info_struct *con_info = *con_cls;

      if (*upload_data_size != 0)
        {
          MHD_post_process (con_info->postprocessor, upload_data,
                            *upload_data_size);
          *upload_data_size = 0;

          return MHD_YES;
        }
      else if (NULL != con_info->answerstring)
        return send_result (connection, con_info->answerstring);
    }

  return send_error_page (connection, errorpage);
}

int
main (int argc, char* argv[])
{
  if (argc < 2) 
  {
    printf("Usage: %s port [espeak_data_parent_dir]\n", argv[0]);
    return 1;
  }

  int port = atoi(argv[1]);

  char* espeak_data_parent_dir;
  if (argc >= 2) 
  {
    espeak_data_parent_dir = argv[2];
  }
  else
  {
    espeak_data_parent_dir = "/usr/share";
  }
  struct MHD_Daemon *daemon;

  int samplerate = espeak_Initialize(AUDIO_OUTPUT_SYNCHRONOUS, 0, espeak_data_parent_dir, 0);

  daemon = MHD_start_daemon (MHD_USE_SELECT_INTERNALLY, port, NULL, NULL,
                             &answer_to_connection, NULL,
                             MHD_OPTION_NOTIFY_COMPLETED, request_completed,
                             NULL, MHD_OPTION_END);
  if (NULL == daemon)
    return 1;

  sigset_t blocked;
  sigemptyset(&blocked);
  sigaddset(&blocked, SIGTERM);
  pthread_sigmask(SIG_BLOCK, &blocked, NULL); // Block SIGINT, SIGTERM and SIGQUIT.
  signal(SIGPIPE, SIG_IGN);                   // Ignore SIGPIPE.
 
  int ret = 0;
  while (ret == 0)
  {
    ret = checkForSignals();
    sleep(3);
  }
  MHD_stop_daemon (daemon);

  return 0;
}
