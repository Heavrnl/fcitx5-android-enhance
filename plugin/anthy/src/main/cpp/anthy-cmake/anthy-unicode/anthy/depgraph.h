/* 付属語グラフのデータ構造 */
#ifndef _depgraph_h_included_
#define _depgraph_h_included_

#include <anthy/segclass.h>
#include <anthy/wtype.h>

struct dep_transition {
  /** 遷移先のノードの番号 0の場合は終端 */
  int next_node;
  /** 品詞 */
  int pos;
  /** 活用形 */
  int ct;
  /* 付属語クラス */
  enum dep_class dc;

  int head_pos;
  int weak;
};

typedef int ondisk_xstr;

struct dep_branch {
  /* 遷移条件の付属語の配列 */
  /** 配列長 */
  int nr_strs;
  /** 遷移条件の配列 */
  xstr **str;
  ondisk_xstr *xstrs;

  /** 遷移先のノード */
  int nr_transitions;
  struct dep_transition *transition;
};

struct dep_node {
  int nr_branch;
  struct dep_branch *branch;
};

/** 自立語の品詞とその後に続く付属語のグラフ中のノードの対応 */
struct wordseq_rule {
  wtype_t wt; /* 自立語の品詞 */
  int node_id; /* この自立語の後ろに続く付属語グラフ中のノードのid */
};

/** 付属語グラフのファイル上での形式 */
struct ondisk_wordseq_rule {
  char wt[8];
  /* ネットワークバイトオーダー */
  int node_id;
};

/* 付属語グラフ */
struct dep_dic {
  char* file_ptr;

  int nrRules;
  int nrNodes;

  /* 自立語からの接続ルール */
  struct ondisk_wordseq_rule *rules;
  /* 付属語間の接続ルール */
  struct dep_node* nodes;
};

#endif
