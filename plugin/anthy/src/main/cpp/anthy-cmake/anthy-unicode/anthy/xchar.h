/* 各種カナや文字のコードと識別関数 */
#ifndef _xchar_h_included_
#define _xchar_h_included_

#include <anthy/xstr.h>

/* 平仮名や各種文字の文字コード */

 /* UCS4 */
#define HK_A 0x3042
#define HK_I 0x3044
#define HK_U 0x3046
#define HK_E 0x3048
#define HK_O 0x304a

#define HK_KA 0x304b
#define HK_KI 0x304d
#define HK_KU 0x304f
#define HK_KE 0x3051
#define HK_KO 0x3053

#define HK_SA 0x3055
#define HK_SI 0x3057
#define HK_SU 0x3059
#define HK_SE 0x305b
#define HK_SO 0x305d

#define HK_TA 0x305f
#define HK_TI 0x3061
#define HK_TU 0x3064
#define HK_TE 0x3066
#define HK_TO 0x3068

#define HK_NA 0x306a
#define HK_NI 0x306b
#define HK_NU 0x306c
#define HK_NE 0x306d
#define HK_NO 0x306e

#define HK_HA 0x306f
#define HK_HI 0x3072
#define HK_HU 0x3075
#define HK_HE 0x3078
#define HK_HO 0x307b

#define HK_MA 0x307e
#define HK_MI 0x307f
#define HK_MU 0x3080
#define HK_ME 0x3081
#define HK_MO 0x3082

#define HK_YA 0x3084
#define HK_YU 0x3086
#define HK_YO 0x3088

#define HK_RA 0x3089
#define HK_RI 0x308a
#define HK_RU 0x308b
#define HK_RE 0x308c
#define HK_RO 0x308d

#define HK_WA 0x308f
#define HK_WI 0x3090
#define HK_WE 0x3091
#define HK_WO 0x3092
#define HK_N 0x3093

#define HK_TT 0x3063

#define HK_XA 0x3041
#define HK_XI 0x3043
#define HK_XU 0x3045
#define HK_XE 0x3047
#define HK_XO 0x3049

#define HK_GA 0x304c
#define HK_GI 0x304e
#define HK_GU 0x3050
#define HK_GE 0x3052
#define HK_GO 0x3054

#define HK_ZA 0x3056
#define HK_ZI 0x3058
#define HK_ZU 0x305a
#define HK_ZE 0x305c
#define HK_ZO 0x305e

#define HK_DA 0x3060
#define HK_DI 0x3062
#define HK_DU 0x3065
#define HK_DE 0x3067
#define HK_DO 0x3069

#define HK_BA 0x3070
#define HK_BI 0x3073
#define HK_BU 0x3076
#define HK_BE 0x3079
#define HK_BO 0x307c

#define HK_PA 0x3071
#define HK_PI 0x3074
#define HK_PU 0x3077
#define HK_PE 0x307a
#define HK_PO 0x307d

#define HK_XYA 0x3083
#define HK_XYU 0x3085
#define HK_XYO 0x3087

#define HK_XWA 0x308e
/*「゛」*/
#define HK_DDOT 0x309b
/* 「ー」 */
#define HK_BAR 0x30fc
#define KK_VU 0x30f4
#define WIDE_COMMA 0xff0c

/* 漢数字 */
#define KJ_1 0x4e00
#define KJ_2 0x4e8c
#define KJ_3 0x4e09
#define KJ_4 0x56db
#define KJ_5 0x4e94
#define KJ_6 0x516d
#define KJ_7 0x4e03
#define KJ_8 0x516b
#define KJ_9 0x4e5d
/* 零 */
#define KJ_0 0x96f6
#define KJ_10 0x5341
#define KJ_100 0x767e
#define KJ_1000 0x5343
#define KJ_10000 0x4e07
#define KJ_100000000 0x5104
#define KJ_1000000000000 0x5146
#define KJ_10000000000000000 0x4eac

/* 全角数字 */
#define WIDE_0 0xff10
#define WIDE_1 0xff11
#define WIDE_2 0xff12
#define WIDE_3 0xff13
#define WIDE_4 0xff14
#define WIDE_5 0xff15
#define WIDE_6 0xff16
#define WIDE_7 0xff17
#define WIDE_8 0xff18
#define WIDE_9 0xff19

#define UCS_GETA 0x3013
#define EUC_GETA 0xa2ae

/**/
int anthy_xchar_to_num(xchar );
xchar anthy_xchar_wide_num_to_num(xchar);
/**/
struct half_kana_table {
  const int src;
  const int dst;
  const int mod;
};
const struct half_kana_table *anthy_find_half_kana(xchar xc);
xchar anthy_lookup_half_wide(xchar xc);

#endif
