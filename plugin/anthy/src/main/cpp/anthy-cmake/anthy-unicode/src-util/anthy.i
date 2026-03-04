%module anthy
%include typemaps.i

%{
#include "include/anthy.h"
%}

%init %{
anthy_init();
%}

%define OUTPUT_TYPEMAP(type, klass)
%typemap(in,numinputs=0) type *OUTPUT(VALUE temp)
{
  temp = rb_funcall(klass, rb_intern("new"), 0);
  Data_Get_Struct(temp, $*ltype, $1);
}

%typemap(argout) type *OUTPUT
{
  if ($result < 0)
    $result = Qnil;
  else
    $result = temp$argnum;
}
%enddef

OUTPUT_TYPEMAP(struct anthy_conv_stat, cConvStat.klass);
OUTPUT_TYPEMAP(struct anthy_segment_stat, cSegmentStat.klass);
OUTPUT_TYPEMAP(struct anthy_prediction_stat, cPredictionStat.klass);

#undef OUTPUT_TYPEMAP

%rename(ConvStat) anthy_conv_stat;
%rename(SegmentStat) anthy_segment_stat;
%rename(PredictionStat) anthy_prediction_stat;

%rename(COMPILED_ENCODING) ANTHY_COMPILED_ENCODING;
%rename(EUC_JP_ENCODING) ANTHY_EUC_JP_ENCODING;
%rename(UTF8_ENCODING) ANTHY_UTF8_ENCODING;

%rename(init) anthy_init;
%rename(quit) anthy_quit;
%rename(conf_override) anthy_conf_override;
%rename(set_personality) anthy_set_personality;

%ignore anthy_create_context;
%ignore anthy_reset_context;
%ignore anthy_release_context;

%ignore anthy_set_string;
%ignore anthy_resize_segment;
%ignore anthy_get_stat;
%ignore anthy_get_segment_stat;
%ignore anthy_get_segment;
%ignore anthy_commit_segment;

%ignore anthy_set_prediction_string;
%ignore anthy_get_prediction_stat;
%ignore anthy_get_prediction;

%ignore anthy_print_context;
%ignore anthy_context_set_encoding;

%{
#define anthy_context_set_string anthy_set_string
#define anthy_context_resize_segment anthy_resize_segment
#define anthy_context_get_stat anthy_get_stat
#define anthy_context_get_segment_stat anthy_get_segment_stat
/* #define anthy_context_get_segment anthy_get_segment */
/* #define anthy_context_commit_segment anthy_commit_segment */
#define anthy_context_print anthy_print_context

#define anthy_context_set_prediction_string anthy_set_prediction_string
#define anthy_context_get_prediction_stat anthy_get_prediction_stat
/* #define anthy_context_get_prediction anthy_get_prediction */
%}

%rename(Context) anthy_context;

%rename(get_version_string) anthy_get_version_string;
/* SWIG says "Segmentation fault" */
/* %alias anthy_get_version_string "version_string"; */
%inline %{
static char *
version_string(void)
{
  return anthy_get_version_string();
}
%}

/* logger */
%ignore anthy_set_logger;

%header %{
static VALUE logger = Qnil;
%}

%init %{
rb_gc_register_address(&logger);
%}

%{
#if 0
} /* for c-mode indentation */
#endif

typedef struct logger_info
{
  VALUE level;
  VALUE message;
} logger_info_t;

static VALUE
invoke_logger(VALUE data)
{
  logger_info_t *info = (logger_info_t *)data;
  return rb_funcall(logger, rb_intern("call"), 2, info->level, info->message);
}

static void
rb_logger(int lv, const char *str)
{
  int state = 0;
  logger_info_t info;

  info.level = INT2NUM(lv);
  info.message = rb_str_new2(str);
  rb_protect(invoke_logger, (VALUE)&info, &state);
  if (state && !NIL_P(ruby_errinfo)) {
    VALUE klass, message, backtrace;
    int i, len;

    klass = rb_funcall(rb_funcall(ruby_errinfo, rb_intern("class"), 0),
                       rb_intern("to_s"), 0);
    message = rb_funcall(ruby_errinfo, rb_intern("message"), 0);
    backtrace = rb_funcall(ruby_errinfo, rb_intern("backtrace"), 0);
    rb_warning("%s: %s\n", StringValueCStr(klass), StringValueCStr(message));

    len = RARRAY(backtrace)->len;
    for (i = 0; i < len; i++) {
      rb_warning("%s\n", StringValueCStr(RARRAY(backtrace)->ptr[i]));
    }
  }
}

#if 0
{ /* for c-mode indentation */
#endif
%}

%inline %{
#if 0
} /* for c-mode indentation */
#endif

static void
set_logger(int level)
{
  if (rb_block_given_p()) {
    logger = Qnil;
    anthy_set_logger(NULL, level);
  } else {
    logger = rb_block_proc();
    anthy_set_logger(rb_logger, level);
  }
}

#if 0
{ /* for c-mode indentation */
#endif
%}

%include include/anthy.h


%freefunc anthy_context "anthy_release_context";

struct anthy_context
{
  %extend
  {
    anthy_context(void)
    {
      return anthy_create_context();
    };

    void reset(void)
    {
      anthy_reset_context(self);
    };

    %alias set_string "string=";
    int set_string(char *);

    void resize_segment(int, int);

    %alias get_stat "stat";
    int get_stat(struct anthy_conv_stat *OUTPUT);

    %alias get_segment_stat "segment_stat";
    int get_segment_stat(int, struct anthy_segment_stat *OUTPUT);

    %alias get_segment "segment";
    VALUE get_segment(int nth_seg, int nth_cand)
    {
      int len;
      char *buffer;
      len = anthy_get_segment(self, nth_seg, nth_cand, NULL, 0);
      buffer = alloca(len + 1);
      len = anthy_get_segment(self, nth_seg, nth_cand, buffer, len + 1);
      if (len < 0) {
        return Qnil;
      } else {
        return rb_str_new2(buffer);
      }
    };

    VALUE commit_segment(int s, int c)
    {
      return (anthy_commit_segment(self, s, c) < 0) ? Qfalse : Qtrue;
    }

    %alias set_prediction_string "prediction_string=";
    int set_prediction_string(const char *);

    %alias get_prediction_stat "prediction_stat";
    int get_prediction_stat(struct anthy_prediction_stat *OUTPUT);

    %alias get_prediction "prediction";
    int get_prediction(int nth_pre)
    {
      int len;
      char *buffer;
      len = anthy_get_prediction(self, nth_pre, NULL, 0);
      buffer = alloca(len + 1);
      len = anthy_get_prediction(self, nth_pre, buffer, len + 1);
      if (len < 0) {
        return Qnil;
      } else {
        return rb_str_new2(buffer);
      }
    };


    void print();

    %alias set_encoding "encoding=";
    void set_encoding(int encoding);
  }
};
