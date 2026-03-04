/*
 * 標準入出力でコマンドを受けたり，変換結果を送るなどの通信を
 * アプリケーション(おもにEmacs)と行うことにより，アプリケーションに
 * Anthyによる入力機能を容易かつ安全に追加できる．
 *
 * Funded by IPA未踏ソフトウェア創造事業 2002 2/26
 * Copyright (C) 2001-2002 UGAWA Tomoharu
 * Copyright (C) 2002-2004 TABATA Yusuke,
 * Copyright (C) 2021 Takao Fujiwara <takao.fujiwara1@gmail.com>
 */
/*
 * *マルチコンテキストの扱いを決めかねている
 * *入出力にstdioを使うかfdを使うか決めかねている
 */

#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <assert.h>

#include <anthy/anthy.h>
#include <anthy/input.h>
#include <anthy/logger.h>

#include "rkconv.h"

#include <config.h>

extern void egg_main(void);

/* 何回次候補を押すと候補の列挙を一覧モードに切替えるか？ */
#define DEFAULT_ENUM_CAND_LIMIT 3


/* キーに対応する定数 */
#define KEY_SHIFT   0x00010000
#define KEY_CTRL    0x00020000
#define KEY_ALT     0x00040000

#define KEY_SPACE   ' '
#define KEY_OPAR    '('
#define KEY_CPAR    ')'

#define KEY_ENTER     0x00000100
#define KEY_DELETE    0x00000200
#define KEY_LEFT      0x00000300
#define KEY_RIGHT     0x00000400
#define KEY_ESC       0x00000500
#define KEY_BACKSPACE 0x00000600
#define KEY_UP        0x00000700
#define KEY_DOWN      0x00000800

#define KEY_CTRL_A      (KEY_CTRL  | 'A')
#define KEY_CTRL_E      (KEY_CTRL  | 'E')
#define KEY_CTRL_J      (KEY_CTRL  | 'J')
#define KEY_CTRL_K      (KEY_CTRL  | 'K')
#define KEY_CTRL_H      (KEY_CTRL  | 'H')
#define KEY_CTRL_D      (KEY_CTRL  | 'D')
#define KEY_SHIFT_LEFT  (KEY_SHIFT | KEY_LEFT)
#define KEY_SHIFT_RIGHT (KEY_SHIFT | KEY_RIGHT)

#define BUF_GROW_SIZE 4096

#define MAX(a,b) ((a) > (b) ? (a) : (b))


/*
 * コマンドにはキーが押されたことを示す普通のコマンドと
 * 高水準な命令のハイレベルコマンドがある．
 */
enum {
  /* ハイレベルコマンド */
  CMDH_IGNORE_ICTXT, CMDH_GETPREEDIT, CMDH_SELECT_CONTEXT,
  CMDH_RELEASE_CONTEXT, CMDH_MAP_EDIT, CMDH_MAP_SELECT,
  CMDH_GET_CANDIDATE, CMDH_SELECT_CANDIDATE, CMDH_CHANGE_TOGGLE,
  CMDH_MAP_CLEAR, CMDH_SET_BREAK_INTO_ROMAN,
  CMDH_SET_PREEDIT_MODE, CMDH_PRINT_CONTEXT,

  /* キーコマンド */
  CMD_SPACE = 1000,
  CMD_ENTER,
  CMD_BACKSPACE,
  CMD_DELETE,
  CMD_UP,
  CMD_ESC,
  CMD_SHIFTARROW,
  CMD_ARROW,
  CMD_KEY,
  CMD_GOBOL,
  CMD_GOEOL,
  CMD_CUT
};

struct high_level_command_type {
  const char* name;
  int cmd;
  int n_arg;
  int opt_arg;
} high_level_command_type[] = {
  /* コンテキストの情報を表示する */
  {"PRINT_CONTEXT",  CMDH_PRINT_CONTEXT,  0, 0},
  /* トグルに使うキーを変更する */
  {"CHANGE_TOGGLE",  CMDH_CHANGE_TOGGLE,  1, 0},
  /* コンテキストを選択する */
  {"SELECT_CONTEXT", CMDH_SELECT_CONTEXT, 1, 0},
  {"RELEASE_CONTEXT", CMDH_RELEASE_CONTEXT, 0, 0},
  {"MAP_CLEAR", CMDH_MAP_CLEAR, 1, 0},
  {"MAP_EDIT",       CMDH_MAP_EDIT,       3, 0},
  {"MAP_SELECT",     CMDH_MAP_SELECT,     1, 0},
  {"GET_CANDIDATE",  CMDH_GET_CANDIDATE,  1, 0},
  {"SELECT_CANDIDATE", CMDH_SELECT_CANDIDATE, 1, 0},
  /* バックスペースでローマ字に戻る */
  {"BREAK_INTO_ROMAN", CMDH_SET_BREAK_INTO_ROMAN, 1, 0},
  /**/
  {"SET_PREEDIT_MODE", CMDH_SET_PREEDIT_MODE, 1, 0},
  /**/
  {NULL, -1, 0, 0}
};

struct command {
  int cmd;
  char** arg;
  int n_arg;
  struct command* next;
};

struct connection {
  char* rbuf;
  int n_rbuf;
  int s_rbuf;
  int rfd;

  char* wbuf;
  int n_wbuf;
  int s_wbuf;
  int wfd;
};

static void send_error(void);

static struct connection* conn;
static struct anthy_input_config* config;
static struct command* command_queue;
static int daemon_sock = -1;
static int anonymous;
static int egg;
static char *personality;
int use_eucjp;

static char *
encode_command_arg(char *a)
{
  int i, j, len;
  char *s;

  len = strlen(a);
  s = malloc(len + 1);
  for(i = 0,j = 0; i < len; i++) {
    if (a[i] != '\\') {
      s[j] = a[i];
      j++;
      continue;
    }
    /* バックスラッシュ */
    i++;
    switch (a[i]) {
    case 0:
    case '\\':
      s[j] = '\\';
      j++;
      break;
    case '\"':
      s[j] = '\"';
      j++;
      break;
    case 'X':
      {
	char buf[5];
	unsigned char *p;
	int num;
	/* ToBeDone エラーチェック */
	strncpy(buf, &a[i+1], 4);
	i+= 5;
	sscanf(buf, "%x", (unsigned int *)&num);
	p = (unsigned char *)buf;
	p[0] = num & 255;
	p[1] = num >> 8;
	j += sprintf(&s[j], "%c%c", buf[1] , buf[0]);
      }
      break;
    }
  }
  s[j] = 0;

  return s;
}

static int
ensure_buffer(char** buf, int* size, int to_size)
{
  if (*size < to_size) {
    *buf = (char*) realloc(*buf, to_size);
    if (*buf == NULL) {
      return -1;
    }
    *size = to_size;
  }
  return 0;
}

static void
kill_connection(struct connection* conn)
{
  (void) conn;
  exit(0);
}

static struct command *
make_command0(int no)
{
  struct command* cmd;

  cmd = (struct command*) malloc(sizeof(struct command));
  cmd->cmd = no;
  cmd->n_arg = 0;
  cmd->arg = NULL;
  cmd->next = NULL;

  return cmd;
}

static struct command *
make_command1(int no, const char* arg1)
{
  struct command* cmd;

  cmd = (struct command*) malloc(sizeof(struct command));
  cmd->cmd = no;
  cmd->n_arg = 1;
  cmd->arg = (char**) malloc(sizeof(char*) * 1);
  cmd->arg[0] = strdup(arg1);
  cmd->next = NULL;

  return cmd;
}

static struct key_name_table {
  const char* name;
  int code;
  int is_modifier;
} key_name_table[] = {
  {"shift",     KEY_SHIFT,     1},
  {"ctrl",      KEY_CTRL,      1},
  {"alt",       KEY_ALT,       1},

  {"space",     KEY_SPACE,     0},
  {"opar",      KEY_OPAR,      0},
  {"cpar",      KEY_CPAR,      0},
  {"enter",     KEY_ENTER,     0},
  {"esc",       KEY_ESC,       0},
  {"backspace", KEY_BACKSPACE, 0},
  {"delete",    KEY_DELETE,    0},
  {"left",      KEY_LEFT,      0},
  {"right",     KEY_RIGHT,     0},
  {"up",        KEY_UP,        0},

  {NULL,        0,             0}
};

/*
 * エンコードされたキーの情報を取得する
 */
static int
read_encoded_key(char** buf)
{
  char* p;
  char* str;

  int key = 0;

  /* 閉じ括弧を探す */
  for (p = *buf + 1; *p; p++) {
    if (*p == ')') {
      break;
    }
  }

  if (*p == '\0') {
    *buf = p;
    return '\0';
  }

  str = *buf + 1;
  *p = '\0';
  *buf = p + 1;

  p = strtok(str, " \t\r");
  if (!p) {
    return '\0';
  }

  do {
    if (p[1] == '\0') {
      return key | *p;
    } else {
      struct key_name_table* e;

      for (e = key_name_table; e->name; e++) {
	if (strcmp(e->name, p) == 0) {
	  key |= e->code;
	  if (e->is_modifier == 0) {
	    return key;
	  }
	}
      }
    }
  } while((p = strtok(NULL, " \t\r")));

  return '\0';
}

static struct high_level_command_type *
find_command_type(char *str)
{
  struct high_level_command_type* cmdn;
  for (cmdn = high_level_command_type; cmdn->name; cmdn++) {
    if (!strcmp(str, cmdn->name)) {
      return cmdn;
    }
  }
  return NULL;
}

/* ハイレベルコマンドをパースする */
static struct command *
make_hl_command(char *buf)
{
  /* high-level command */
  struct high_level_command_type* cmdn;
  struct command* cmd = NULL;
  char* p;
  int i;

  /* コマンドの種類を調べる */
  p = strtok(buf, " \t\r");
  if (!p) {
    return NULL;
  }
  cmdn = find_command_type(p);
  if (!cmdn) {
    return NULL;
  }

  /* コマンドを作る */
  cmd = (struct command*) malloc(sizeof(struct command));
  cmd->cmd = cmdn->cmd;
  cmd->n_arg = cmdn->n_arg;
  if (cmd->n_arg > 0) {
    cmd->arg = (char**) malloc(sizeof(char*) * cmd->n_arg);
  } else {
    cmd->arg = NULL;
  }
  for (i = 0; i < cmd->n_arg; i++) {
    p = strtok(NULL, " \t\r");
    if (!p) {
      while (i-- > 0)
	free(cmd->arg[i]);
      free(cmd->arg);
      free(cmd);
      return NULL;
    }
    cmd->arg[i] = encode_command_arg(p);
  }
  while ((p = strtok(NULL, " \t\r"))) {
    cmd->n_arg++;
    cmd->arg = (char**) realloc(cmd->arg, sizeof(char*) * cmd->n_arg);
    cmd->arg[cmd->n_arg - 1] = encode_command_arg(p);
  }
  cmd->next = NULL;
  return cmd;
}

/* 普通のコマンドをパースする */
static struct command *
make_ll_command(char *buf)
{
  struct command* cmd_head = NULL;
  struct command* cmd = NULL;
  char* p;

  for (p = buf; *p; ) {
    struct command* cmd0 = NULL;
    int c;

    if (isspace((int)(unsigned char) *p)) {
      p++;
      continue;
    } else if (*p == '(') {
      c = read_encoded_key(&p);
    } else {
      c = *p++;
    }

    switch (c) {
    case '\0':
      break;
    case KEY_SPACE:
      cmd0 = make_command0(CMD_SPACE);
      break;
    case KEY_CTRL_J:
    case KEY_ENTER:
    case KEY_DOWN:
      cmd0 = make_command0(CMD_ENTER);
      break;
    case KEY_BACKSPACE:
    case KEY_CTRL_H:
      cmd0 = make_command0(CMD_BACKSPACE);
      break;
    case KEY_DELETE:
    case KEY_CTRL_D:
      cmd0 = make_command0(CMD_DELETE);
      break;
    case KEY_SHIFT_LEFT:
      cmd0 = make_command1(CMD_SHIFTARROW, "-1");
      break;
    case KEY_SHIFT_RIGHT:
      cmd0 = make_command1(CMD_SHIFTARROW, "1");
      break;
    case KEY_LEFT:
      cmd0 = make_command1(CMD_ARROW, "-1");
      break;
    case KEY_RIGHT:
      cmd0 = make_command1(CMD_ARROW, "1");
      break;
    case KEY_UP:
      cmd0 = make_command0(CMD_UP);
      break;
    case KEY_ESC:
      cmd0 = make_command0(CMD_ESC);
      break;
    case KEY_CTRL_A:
      cmd0 = make_command0(CMD_GOBOL);
      break;
    case KEY_CTRL_E:
      cmd0 = make_command0(CMD_GOEOL);
      break;
    case KEY_CTRL_K:
      cmd0 = make_command0(CMD_CUT);
      break;
    default:
      if ((c & 0xffffff80) == 0) {
	/* ASCII文字 */
	char str[2];

	str[0] = (char)c;
	str[1] = '\0';
	/* cmd_key */
	cmd0 = make_command1(CMD_KEY, str);
      }
      break;
    }

    if (cmd0) {
      if (cmd) {
	cmd->next = cmd0;
      } else {
	cmd_head = cmd0;
      }
      cmd = cmd0;
    }
  } /* for (p) */

  if (cmd) {
    cmd->next = make_command0(CMDH_GETPREEDIT);
  } else {
    cmd_head = make_command0(CMDH_GETPREEDIT);
  }

  return cmd_head;
}

static struct command*
make_command(char* buf)
{

  if (*buf == ' ') {
    /* ハイレベルコマンド */
    struct command *cmd;
    cmd = make_hl_command(buf);
    if (!cmd) {
      send_error();
    }
    return cmd;
  }
  return make_ll_command(buf);
}

static int
proc_connection(void)
{
  fd_set rfds;
  fd_set wfds;
  int max_fd;
  int ret;

  max_fd = -1;
  FD_ZERO(&rfds);
  FD_ZERO(&wfds);
  if (daemon_sock >= 0) {
    max_fd = daemon_sock;
    FD_SET(daemon_sock, &rfds);
  }
  max_fd = MAX(conn->rfd, max_fd);
  assert(conn->rfd >= 0);
  FD_SET(conn->rfd, &rfds);
  assert(conn->rfd >= 0);
  if (conn->n_wbuf > 0) {
    max_fd = MAX(conn->wfd, max_fd);
    assert(conn->wfd >= 0);
    FD_SET(conn->wfd, &wfds);
  }

  if (max_fd == -1)
    return -1;

  ret = select(max_fd + 1, &rfds, &wfds, NULL, NULL);
  if (ret < 0) {
    return -1;
  }

  if (conn->n_wbuf > 0 && FD_ISSET(conn->wfd, &wfds)) {
    ret = write(conn->wfd, conn->wbuf, conn->n_wbuf);
    if (ret <= 0) {
      kill_connection (conn);
    } else {
      conn->n_wbuf -= ret;
      if (conn->n_wbuf > 0) {
	memmove(conn->wbuf, conn->wbuf + ret, conn->n_wbuf);
      }
    }
  }

  if (FD_ISSET(conn->rfd, &rfds)) {
    ensure_buffer(&conn->rbuf, &conn->s_rbuf,
		  conn->n_rbuf + BUF_GROW_SIZE);
    ret = read(conn->rfd,
	       conn->rbuf + conn->n_rbuf, conn->s_rbuf - conn->n_rbuf);
    if (ret <= 0) {
      kill_connection (conn);
    } else {
      conn->n_rbuf += ret;
    }
  }
  return 0;
}

static struct command *
read_command(void)
{
  struct command* cmd;

AGAIN:
  if (command_queue != NULL) {
    cmd = command_queue;
    command_queue = cmd->next;
    return cmd;
  }

  while (1) {
    char* p;
    for (p = conn->rbuf; p < conn->rbuf + conn->n_rbuf; p++) {
      if (*p == '\n') {
	*p = '\0';
	cmd = make_command(conn->rbuf);
	conn->n_rbuf -= p + 1 - conn->rbuf;
	memmove(conn->rbuf, p + 1, conn->n_rbuf);
	if (cmd) {
	  command_queue = cmd;
	  goto AGAIN;
	}
      }
    }
    if (proc_connection() == -1) {
      return NULL;
    }
  }
}

static void
write_reply(const char* buf)
{
  printf("%s", buf);
}

static void
send_error(void)
{
  write_reply("ERR\r\n");
}

static void
send_ok(void)
{
  write_reply("OK\r\n");
}

static void
send_number10(int num)
{
  char buf[20];

  sprintf(buf, "%d", num);
  write_reply(buf);
}

static void
send_string(const char* str)
{
  write_reply(str);
}

static void
send_quote_string(const char* str)
{
  char buf[20]; /* このぐらいあれば大抵大丈夫 */
  const char *p;
  char *q, *end;

  end = buf + sizeof(buf) - 2;
  for (q = buf, p = str; *p;) {
    if (q >= end) {
      *q = '\0';
      write_reply(buf);
      q = buf;
    }

    switch (*p) {
    case '\"':
    case '\\':
      *q++ = '\\';
      break;
    default:
      break;
    }

    *q++ = *p++;
  }
  *q = '\0';
  write_reply(buf);
}

static void
send_preedit(struct anthy_input_preedit* pedit)
{
  send_string("(");
  send_number10(pedit->state);

  if (pedit->commit != NULL) {
    send_string(" ((COMMIT) \"");
    send_quote_string(pedit->commit);
    send_string("\")");
  }

  if (pedit->cut_buf != NULL) {
    send_string(" ((CUTBUF) \"");
    send_quote_string(pedit->cut_buf);
    send_string("\")");
  }

  switch (pedit->state) {
  case ANTHY_INPUT_ST_OFF:
  case ANTHY_INPUT_ST_NONE:
    break;
  case ANTHY_INPUT_ST_EDIT:
  case ANTHY_INPUT_ST_CONV:
  case ANTHY_INPUT_ST_CSEG:
    {
      struct anthy_input_segment* seg;

      for (seg = pedit->segment; seg; seg = seg->next) {
	if (seg->str == NULL) {
	  if (seg->flag & ANTHY_INPUT_SF_CURSOR)
	    send_string(" cursor");
	} else {
	  if (seg->flag & ANTHY_INPUT_SF_CURSOR) {
	    if (seg->flag & ANTHY_INPUT_SF_ENUM)
	      send_string(" ((UL RV ENUM) \"");
	    else if (seg->flag & ANTHY_INPUT_SF_ENUM_REVERSE)
	      send_string(" ((UL RV ENUMR) \"");
	    else
	      send_string(" ((UL RV) \"");
	  } else
	    send_string(" ((UL) \"");

	  send_quote_string(seg->str);
	  send_string("\" ");
	  send_number10(seg->cand_no);
	  send_string(" ");
	  send_number10(seg->nr_cand);
	  send_string(")");
	}
      }
    }
    break;
  }

  send_string(")\r\n");
}

static void
send_single_candidate(struct anthy_input_segment* seg)
{
  send_string("(\"");
  send_quote_string(seg->str);
  send_string("\" ");
  send_number10(seg->cand_no);
  send_string(" ");
  send_number10(seg->nr_cand);
  send_string(")\r\n");
}

static void
free_command(struct command* cmd)
{
  int i;

  for (i = 0; i < cmd->n_arg; i++)
    free(cmd->arg[i]);
  free(cmd->arg);
  free(cmd);
}

struct input_context_list {
  int id;
  struct anthy_input_context* ictx;
  struct input_context_list* next;
};

static struct input_context_list* ictx_list = NULL;

static void
new_input_context(int id)
{
  struct input_context_list* ictxl;

  ictxl =
    (struct input_context_list*) malloc(sizeof (struct input_context_list));
  if (!ictxl) {
    anthy_log(0, "Failed malloc in %s:%d\n", __FILE__, __LINE__);
    return;
  }
  ictxl->id = id;
  ictxl->ictx = anthy_input_create_context(config);
  ictxl->next = ictx_list;
  ictx_list = ictxl;
}

static struct anthy_input_context*
get_current_input_context(void)
{
  if (ictx_list == NULL)
    new_input_context(0);

  if (!ictx_list)
    return NULL;
  return ictx_list->ictx;
}

static void
cmdh_get_preedit(struct anthy_input_context* ictx)
{
  struct anthy_input_preedit* pedit;

  pedit = anthy_input_get_preedit(ictx);
  send_preedit(pedit);
  anthy_input_free_preedit(pedit);
}

static void
cmdh_select_input_context(struct command* cmd)
{
  int id;
  struct input_context_list** p;

  id = atoi(cmd->arg[0]);
  for (p = &ictx_list; *p; p = &(*p)->next) {
    if ((*p)->id == id) {
      struct input_context_list* sel;
      sel = *p;
      *p = sel->next;
      sel->next = ictx_list;
      ictx_list = sel;
      send_ok();
      return;
    }
  }

  new_input_context(id);
  send_ok();
}

static void
cmdh_release_input_context(struct command* cmd)
{
  struct input_context_list* sel;
  (void)cmd;
  if (!ictx_list) {
      send_ok();
    return ;
  }
  sel = ictx_list;
  ictx_list = ictx_list->next;
  anthy_input_free_context(sel->ictx);
  free(sel);
  send_ok();
}

static void
cmdh_change_toggle(struct command *cmd)
{
  int toggle;
  int ret;

  assert(cmd->arg);
  if (!cmd->arg[0]) {
    anthy_log(0, "cmdh_change_toggle should have one argument.\n");
    send_error();
    return;
  }
  toggle = cmd->arg[0][0];
  ret = anthy_input_edit_toggle_config(config, toggle);

  if (ret != 0) {
    send_error();
    return;
  }
  anthy_input_change_config(config);
  send_ok();
}

static void
cmdh_map_clear(struct command *cmd)
{
  assert(cmd->arg);
  anthy_input_clear_rk_config(config, atoi(cmd->arg[0]));
  anthy_input_change_config(config);
  send_ok();
}

static void
cmdh_set_break_into_roman(struct command *cmd)
{
  assert(cmd->arg);
  anthy_input_break_into_roman_config(config, atoi(cmd->arg[0]));
  anthy_input_change_config(config);
  send_ok();
}

static void
cmdh_set_preedit_mode(struct command *cmd)
{
  assert(cmd->arg);
  anthy_input_preedit_mode_config(config, atoi(cmd->arg[0]));
  anthy_input_change_config(config);
  send_ok();
}

static void
cmdh_map_edit(struct command* cmd)
{
  int map_no;
  int ret;

  assert(cmd->arg);
  /* MAP,from,to */
  map_no = atoi(cmd->arg[0]);
  ret = anthy_input_edit_rk_config(config, map_no,
				   cmd->arg[1], cmd->arg[2], NULL);

  if (ret != 0) {
    send_error();
    return;
  }
  anthy_input_change_config(config);
  send_ok();
}

static void
cmdh_map_select(struct anthy_input_context* ictx,
		struct command* cmd)
{
  char* map_name;
  int map_no;

  assert(cmd->arg);
  map_name = cmd->arg[0];
  if (strcmp(map_name, "alphabet") == 0)
    map_no = ANTHY_INPUT_MAP_ALPHABET;
  else if (strcmp(map_name, "hiragana") == 0)
    map_no = ANTHY_INPUT_MAP_HIRAGANA;
  else if (strcmp(map_name, "katakana") == 0)
    map_no = ANTHY_INPUT_MAP_KATAKANA;
  else if (strcmp(map_name, "walphabet") == 0)
    map_no = ANTHY_INPUT_MAP_WALPHABET;
  else if (strcmp(map_name, "hankaku_kana") == 0)
    map_no = ANTHY_INPUT_MAP_HANKAKU_KANA;
  else {
    send_error();
    return;
  }

  anthy_input_map_select(ictx, map_no);
  send_ok();
}

static void
cmdh_get_candidate(struct anthy_input_context* ictx,
		   struct command* cmd)
{
  struct anthy_input_segment* seg;
  int cand_no;

  assert(cmd->arg);
  cand_no = atoi(cmd->arg[0]);

  seg = anthy_input_get_candidate(ictx, cand_no);
  if (seg == NULL)
    send_error();
  else {
    send_single_candidate(seg);
    anthy_input_free_segment(seg);
  }
}

static void
cmdh_select_candidate(struct anthy_input_context* ictx,
		      struct command* cmd)
{
  int ret;
  int cand_no;

  assert(cmd->arg);
  cand_no = atoi(cmd->arg[0]);
  ret = anthy_input_select_candidate(ictx, cand_no);
  if (ret < 0) {
    send_error();
  } else {
    cmdh_get_preedit(ictx);
  }
}

static void
cmd_shift_arrow(struct anthy_input_context* ictx,
		struct command* cmd)
{
  int lr;
  assert(cmd->arg);
  lr = atoi(cmd->arg[0]);
  anthy_input_resize(ictx, lr);
}

static void
cmd_arrow(struct anthy_input_context* ictx, struct command* cmd)
{
  int lr;
  assert(cmd->arg);
  lr = atoi(cmd->arg[0]);
  anthy_input_move(ictx, lr);
}

static void
cmd_key(struct anthy_input_context* ictx, struct command* cmd)
{
  assert(cmd->arg);
  anthy_input_str(ictx, cmd->arg[0]);
}

/*
 * コマンドをディスパッチする
 */
static void
dispatch_command(struct anthy_input_context* ictx,
		 struct command* cmd)
{
  switch (cmd->cmd) {
  case CMDH_IGNORE_ICTXT:
    send_error();
    break;
  case CMDH_PRINT_CONTEXT:
    /* Dirty implementation, would cause corrpution.*/
    {
      anthy_context_t ac = anthy_input_get_anthy_context(ictx);
      if (ac) {
	anthy_print_context(ac);
      }
    }
    break;
  case CMDH_GETPREEDIT:
    cmdh_get_preedit(ictx);
    break;
  case CMDH_SELECT_CONTEXT:
    cmdh_select_input_context(cmd);
    break;
  case CMDH_RELEASE_CONTEXT:
    cmdh_release_input_context(cmd);
    break;
  case CMDH_MAP_EDIT:
    cmdh_map_edit(cmd);
    break;
  case CMDH_CHANGE_TOGGLE:
    cmdh_change_toggle(cmd);
    break;
  case CMDH_MAP_CLEAR:
    cmdh_map_clear(cmd);
    break;
  case CMDH_MAP_SELECT:
    cmdh_map_select(ictx, cmd);
    break;
  case CMDH_GET_CANDIDATE:
    cmdh_get_candidate(ictx, cmd);
    break;
  case CMDH_SELECT_CANDIDATE:
    cmdh_select_candidate(ictx, cmd);
    break;
  case CMDH_SET_BREAK_INTO_ROMAN:
    cmdh_set_break_into_roman(cmd);
    break;
  case CMDH_SET_PREEDIT_MODE:
    cmdh_set_preedit_mode(cmd);
    break;
    /* key commands follows */

  case CMD_SPACE:
    anthy_input_space(ictx);
    break;
  case CMD_ENTER:
    anthy_input_commit(ictx);
    break;
  case CMD_BACKSPACE:
    anthy_input_erase_prev(ictx);
    break;
  case CMD_DELETE:
    anthy_input_erase_next(ictx);
    break;
  case CMD_UP:
    anthy_input_prev_candidate(ictx);
    break;
  case CMD_ESC:
    anthy_input_quit(ictx);
    break;
  case CMD_SHIFTARROW:
    cmd_shift_arrow(ictx, cmd);
    break;
  case CMD_ARROW:
    cmd_arrow(ictx, cmd);
    break;
  case CMD_KEY:
    cmd_key(ictx, cmd);
    break;
  case CMD_GOBOL:
    anthy_input_beginning_of_line(ictx);
    break;
  case CMD_GOEOL:
    anthy_input_end_of_line(ictx);
    break;
  case CMD_CUT:
    anthy_input_cut(ictx);
    break;
  }
}

static void
main_loop(void)
{
  struct anthy_input_context* ictx;
  struct command* cmd;

  while (1) {
    cmd = read_command();
    if (!cmd)
      break;
    ictx = get_current_input_context();

    dispatch_command(ictx, cmd);
    fflush(stdout);

    free_command(cmd);
  }
}

static void
print_version(void)
{
  printf(PACKAGE "-agent "VERSION "\n");
  exit(0);
}

static void
parse_args(int argc, char **argv)
{
  int i;
  char *conffile = NULL, *dir = NULL, *dic = NULL;


  for (i = 1; i < argc; i++) {
    char *str = argv[i];
    if (!strcmp("--version", str)) {
      print_version();
    } else if (!strcmp("--anonymous", str)) {
      anonymous = 1;
    } else if (!strcmp("--egg", str)) {
      egg = 1;
    } else if (!strncmp("--personality=", str, 14)) {
      personality = &str[14];
    } else if (!strcmp("--eucjp", str)) {
      use_eucjp = 1;
    } else if (i < argc - 1) {
      char *arg = argv[i+1];
      if (!strcmp("--dir", str)) {
	dir = arg;
	i++;
      } else if (!strcmp("--dic", str)) {
	dic = arg;
	i++;
      } else if (!strcmp("--conffile", str)) {
	conffile = arg;
	i++;
      }
    }
  }
  if (conffile) {
    anthy_conf_override("CONFFILE", conffile);
  }
  if (dir) {
    anthy_conf_override("ANTHYDIR", dir);
  }
  if (dic) {
    anthy_conf_override("SDIC", dic);
  }
}

int
main(int argc, char **argv)
{
  parse_args(argc, argv);
  if (anthy_input_init()) {
    printf("Failed to init anthy\n");
    exit(0);
  }
  if (anonymous) {
    anthy_input_set_personality("");
  } else if (personality) {
    anthy_input_set_personality(personality);
  }

  if (egg) {
    egg_main();
    anthy_quit();
  } else {
    config = anthy_input_create_config();
    if (!(conn = (struct connection*) malloc(sizeof(struct connection)))) {
      anthy_log(0, "Failed malloc in %s:%d\n", __FILE__, __LINE__);
      return 1;
    }
    conn->rbuf = NULL;
    conn->n_rbuf = 0;
    conn->s_rbuf = 0;
    conn->rfd = 0;
    conn->wbuf = NULL;
    conn->n_wbuf = 0;
    conn->s_wbuf = 0;
    conn->wfd = 1;

    main_loop();
  }
  return 0;
}
