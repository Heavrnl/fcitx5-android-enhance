/*
 * roma kana converter rule structure
 *
 * $Id: rkmap.h,v 1.6 2002/11/05 15:38:58 yusuke Exp $
 */

static const struct rk_rule rk_rule_alphabet[] = 
{
	{"a", "a", NULL},
	{"b", "b", NULL},
	{"c", "c", NULL},
	{"d", "d", NULL},
	{"e", "e", NULL},
	{"f", "f", NULL},
	{"g", "g", NULL},
	{"h", "h", NULL},
	{"i", "i", NULL},
	{"j", "j", NULL},
	{"k", "k", NULL},
	{"l", "l", NULL},
	{"m", "m", NULL},
	{"n", "n", NULL},
	{"o", "o", NULL},
	{"p", "p", NULL},
	{"q", "q", NULL},
	{"r", "r", NULL},
	{"s", "s", NULL},
	{"t", "t", NULL},
	{"u", "u", NULL},
	{"v", "v", NULL},
	{"w", "w", NULL},
	{"x", "x", NULL},
	{"y", "y", NULL},
	{"z", "z", NULL},
	{"A", "A", NULL},
	{"B", "B", NULL},
	{"C", "C", NULL},
	{"D", "D", NULL},
	{"E", "E", NULL},
	{"F", "F", NULL},
	{"G", "G", NULL},
	{"H", "H", NULL},
	{"I", "I", NULL},
	{"J", "J", NULL},
	{"K", "K", NULL},
	{"L", "L", NULL},
	{"M", "M", NULL},
	{"N", "N", NULL},
	{"O", "O", NULL},
	{"P", "P", NULL},
	{"Q", "Q", NULL},
	{"R", "R", NULL},
	{"S", "S", NULL},
	{"T", "T", NULL},
	{"U", "U", NULL},
	{"V", "V", NULL},
	{"W", "W", NULL},
	{"X", "X", NULL},
	{"Y", "Y", NULL},
	{"Z", "Z", NULL},

	{NULL, NULL, NULL}
};

static const struct rk_rule rk_rule_walphabet[] = 
{
	{"a", "ａ", NULL},
	{"b", "ｂ", NULL},
	{"c", "ｃ", NULL},
	{"d", "ｄ", NULL},
	{"e", "ｅ", NULL},
	{"f", "ｆ", NULL},
	{"g", "ｇ", NULL},
	{"h", "ｈ", NULL},
	{"i", "ｉ", NULL},
	{"j", "ｊ", NULL},
	{"k", "ｋ", NULL},
	{"l", "ｌ", NULL},
	{"m", "ｍ", NULL},
	{"n", "ｎ", NULL},
	{"o", "ｏ", NULL},
	{"p", "ｐ", NULL},
	{"q", "ｑ", NULL},
	{"r", "ｒ", NULL},
	{"s", "ｓ", NULL},
	{"t", "ｔ", NULL},
	{"u", "ｕ", NULL},
	{"v", "ｖ", NULL},
	{"w", "ｗ", NULL},
	{"x", "ｘ", NULL},
	{"y", "ｙ", NULL},
	{"z", "ｚ", NULL},
	{"A", "Ａ", NULL},
	{"B", "Ｂ", NULL},
	{"C", "Ｃ", NULL},
	{"D", "Ｄ", NULL},
	{"E", "Ｅ", NULL},
	{"F", "Ｆ", NULL},
	{"G", "Ｇ", NULL},
	{"H", "Ｈ", NULL},
	{"I", "Ｉ", NULL},
	{"J", "Ｊ", NULL},
	{"K", "Ｋ", NULL},
	{"L", "Ｌ", NULL},
	{"M", "Ｍ", NULL},
	{"N", "Ｎ", NULL},
	{"O", "Ｏ", NULL},
	{"P", "Ｐ", NULL},
	{"Q", "Ｑ", NULL},
	{"R", "Ｒ", NULL},
	{"S", "Ｓ", NULL},
	{"T", "Ｔ", NULL},
	{"U", "Ｕ", NULL},
	{"V", "Ｖ", NULL},
	{"W", "Ｗ", NULL},
	{"X", "Ｘ", NULL},
	{"Y", "Ｙ", NULL},
	{"Z", "Ｚ", NULL},

	{NULL, NULL, NULL}
};

#define SKK_LIKE_KIGO_MAP \
	{"z/", "・", NULL}, \
	{"z[", "「", NULL}, \
	{"z]", "」", NULL}, \
	{"z,", "‥", NULL}, \
	{"z.", "…", NULL}, \
	{"z-", "〜", NULL}, \
	{"zh", "←", NULL}, \
	{"zj", "↓", NULL}, \
	{"zk", "↑", NULL}, \
	{"zl", "→", NULL}

static const struct rk_rule rk_rule_hiragana[] =
{
	SKK_LIKE_KIGO_MAP,

	{"a", "あ", NULL},
	{"i", "い", NULL},
	{"u", "う", NULL},
	{"e", "え", NULL},
	{"o", "お", NULL},

	{"xa", "ぁ", NULL},
	{"xi", "ぃ", NULL},
	{"xu", "ぅ", NULL},
	{"xe", "ぇ", NULL},
	{"xo", "ぉ", NULL},
	
	{"ka", "か", NULL},
	{"ki", "き", NULL},
	{"ku", "く", NULL},
	{"ke", "け", NULL},
	{"ko", "こ", NULL},

	{"kya", "きゃ", NULL},
	{"kyi", "きぃ", NULL},
	{"kyu", "きゅ", NULL},
	{"kye", "きぇ", NULL},
	{"kyo", "きょ", NULL},
    
	{"k", "っ", "k"},

	{"ga", "が", NULL},
	{"gi", "ぎ", NULL},
	{"gu", "ぐ", NULL},
	{"ge", "げ", NULL},
	{"go", "ご", NULL},

	{"gya", "ぎゃ", NULL},
	{"gyi", "ぎぃ", NULL},
	{"gyu", "ぎゅ", NULL},
	{"gye", "ぎぇ", NULL},
	{"gyo", "ぎょ", NULL},
    
	{"g", "っ", "g"},

	{"sa", "さ", NULL},
	{"si", "し", NULL},
	{"su", "す", NULL},
	{"se", "せ", NULL},
	{"so", "そ", NULL},

	{"sya", "しゃ", NULL},
	{"syi", "しぃ", NULL},
	{"syu", "しゅ", NULL},
	{"sye", "しぇ", NULL},
	{"syo", "しょ", NULL},
    
	{"sha", "しゃ", NULL},
	{"shi", "し", NULL},
	{"shu", "しゅ", NULL},
	{"she", "しぇ", NULL},
	{"sho", "しょ", NULL},

	{"s", "っ", "s"},

	{"za", "ざ", NULL},
	{"zi", "じ", NULL},
	{"zu", "ず", NULL},
	{"ze", "ぜ", NULL},
	{"zo", "ぞ", NULL},

	{"zya", "じゃ", NULL},
	{"zyi", "じぃ", NULL},
	{"zyu", "じゅ", NULL},
	{"zye", "じぇ", NULL},
	{"zyo", "じょ", NULL},

	{"z", "っ", "z"},
    
	{"ja", "じゃ", NULL},
	{"ji", "じ", NULL},
	{"ju", "じゅ", NULL},
	{"je", "じぇ", NULL},
	{"jo", "じょ", NULL},

	{"jya", "じゃ", NULL},
	{"jyi", "じぃ", NULL},
	{"jyu", "じゅ", NULL},
	{"jye", "じぇ", NULL},
	{"jyo", "じょ", NULL},
    
	{"j", "っ", "j"},
    
	{"ta", "た", NULL},
	{"ti", "ち", NULL},
	{"tu", "つ", NULL},
	{"te", "て", NULL},
	{"to", "と", NULL},
	{"t.", "t", NULL},  /* 最新版の alt-cannadic の単漢字辞書への対応 */

	{"tya", "ちゃ", NULL},
	{"tyi", "ちぃ", NULL},
	{"tyu", "ちゅ", NULL},
	{"tye", "ちぇ", NULL},
	{"tyo", "ちょ", NULL},
    
	{"tha", "てぁ", NULL},
	{"thi", "てぃ", NULL},
	{"thu", "てゅ", NULL},
	{"the", "てぇ", NULL},
	{"tho", "てょ", NULL},

	{"t", "っ", "tc"},

	{"cha", "ちゃ", NULL},
	{"chi", "ち", NULL},
	{"chu", "ちゅ", NULL},
	{"che", "ちぇ", NULL},
	{"cho", "ちょ", NULL},

	{"tsu", "つ", NULL},
	{"xtu", "っ", NULL},
	{"xtsu", "っ", NULL},

	{"c", "っ", "c"},

	{"da", "だ", NULL},
	{"di", "ぢ", NULL},
	{"du", "づ", NULL},
	{"de", "で", NULL},
	{"do", "ど", NULL},

	{"dya", "ぢゃ", NULL},
	{"dyi", "ぢぃ", NULL},
	{"dyu", "ぢゅ", NULL},
	{"dye", "ぢぇ", NULL},
	{"dyo", "ぢょ", NULL},

	{"dha", "でゃ", NULL},
	{"dhi", "でぃ", NULL},
	{"dhu", "でゅ", NULL},
	{"dhe", "でぇ", NULL},
	{"dho", "でょ", NULL},
    
	{"d", "っ", "d"},

	{"na", "な", NULL},
	{"ni", "に", NULL},
	{"nu", "ぬ", NULL},
	{"ne", "ね", NULL},
	{"no", "の", NULL},

	{"nya", "にゃ", NULL},
	{"nyi", "にぃ", NULL},
	{"nyu", "にゅ", NULL},
	{"nye", "にぇ", NULL},
	{"nyo", "にょ", NULL},

	{"n", "ん", NULL},
	{"nn", "ん", NULL},

	{"ha", "は", NULL},
	{"hi", "ひ", NULL},
	{"hu", "ふ", NULL},
	{"he", "へ", NULL},
	{"ho", "ほ", NULL},

	{"hya", "ひゃ", NULL},
	{"hyi", "ひぃ", NULL},
	{"hyu", "ひゅ", NULL},
	{"hye", "ひぇ", NULL},
	{"hyo", "ひょ", NULL},

	{"h", "っ", "h"},
    
	{"fa", "ふぁ", NULL},
	{"fi", "ふぃ", NULL},
	{"fu", "ふ", NULL},
	{"fe", "ふぇ", NULL},
	{"fo", "ふぉ", NULL},

	{"fya", "ふゃ", NULL},
	{"fyi", "ふぃ", NULL},
	{"fyu", "ふゅ", NULL},
	{"fye", "ふぇ", NULL},
	{"fyo", "ふょ", NULL},

	{"f", "っ", "f"},
    
	{"ba", "ば", NULL},
	{"bi", "び", NULL},
	{"bu", "ぶ", NULL},
	{"be", "べ", NULL},
	{"bo", "ぼ", NULL},
    
	{"bya", "びゃ", NULL},
	{"byi", "びぃ", NULL},
	{"byu", "びゅ", NULL},
	{"bye", "びぇ", NULL},
	{"byo", "びょ", NULL},

	{"b", "っ", "b" },

	{"pa", "ぱ", NULL},
	{"pi", "ぴ", NULL},
	{"pu", "ぷ", NULL},
	{"pe", "ぺ", NULL},
	{"po", "ぽ", NULL},

	{"pya", "ぴゃ", NULL},
	{"pyi", "ぴぃ", NULL},
	{"pyu", "ぴゅ", NULL},
	{"pye", "ぴぇ", NULL},
	{"pyo", "ぴょ", NULL},
    
	{"p", "っ", "p"},
    
	{"ma", "ま", NULL},
	{"mi", "み", NULL},
	{"mu", "む", NULL},
	{"me", "め", NULL},
	{"mo", "も", NULL},

	{"mya", "みゃ", NULL},
	{"myi", "みぃ", NULL},
	{"myu", "みゅ", NULL},
	{"mye", "みぇ", NULL},
	{"myo", "みょ", NULL},

	{"m", "ん", "bp"},
	{"m", "っ", "m"},

	{"y", "っ", "y"},
	{"ya", "や", NULL},
	{"yu", "ゆ", NULL},
	{"yo", "よ", NULL},

	{"xya", "ゃ", NULL},
	{"xyu", "ゅ", NULL},
	{"xyo", "ょ", NULL},

	{"r", "っ", "r"},
	{"ra", "ら", NULL},
	{"ri", "り", NULL},
	{"ru", "る", NULL},
	{"re", "れ", NULL},
	{"ro", "ろ", NULL},

	{"rya", "りゃ", NULL},
	{"ryi", "りぃ", NULL},
	{"ryu", "りゅ", NULL},
	{"rye", "りぇ", NULL},
	{"ryo", "りょ", NULL},

	{"xwa", "ゎ", NULL},
	{"wa", "わ", NULL},
	{"wi", "うぃ", NULL},
	{"xwi", "ゐ", NULL},
	{"we", "うぇ", NULL},
	{"xwe", "ゑ", NULL},
	{"wo", "を", NULL},
    
	{"va", "う゛ぁ", NULL},
	{"vi", "う゛ぃ", NULL},
	{"vu", "う゛", NULL},
	{"ve", "う゛ぇ", NULL},
	{"vo", "う゛ぉ", NULL},

	{"VA", "ヴァ", NULL},   /* Debian系由来？の「ヴ」パッチの動作試験用 */
	{"VI", "ヴィ", NULL},
	{"VU", "ヴ", NULL},
	{"VE", "ヴェ", NULL},
	{"VO", "ヴォ", NULL},

	{NULL, NULL, NULL}
};

static const struct rk_rule rk_rule_katakana[] =
{
	SKK_LIKE_KIGO_MAP,

	{"a", "ア", NULL},
	{"i", "イ", NULL},
	{"u", "ウ", NULL},
	{"e", "エ", NULL},
	{"o", "オ", NULL},

	{"xa", "ァ", NULL},
	{"xi", "ィ", NULL},
	{"xu", "ゥ", NULL},
	{"xe", "ェ", NULL},
	{"xo", "ォ", NULL},
	
	{"ka", "カ", NULL},
	{"ki", "キ", NULL},
	{"ku", "ク", NULL},
	{"ke", "ケ", NULL},
	{"ko", "コ", NULL},

	{"kya", "キャ", NULL},
	{"kyi", "キィ", NULL},
	{"kyu", "キュ", NULL},
	{"kye", "キェ", NULL},
	{"kyo", "キョ", NULL},
    
	{"k", "ッ", "k"},

	{"ga", "ガ", NULL},
	{"gi", "ギ", NULL},
	{"gu", "グ", NULL},
	{"ge", "ゲ", NULL},
	{"go", "ゴ", NULL},

	{"gya", "ギャ", NULL},
	{"gyi", "ギィ", NULL},
	{"gyu", "ギュ", NULL},
	{"gye", "ギェ", NULL},
	{"gyo", "ギョ", NULL},
    
	{"g", "ッ", "g"},

	{"sa", "サ", NULL},
	{"si", "シ", NULL},
	{"su", "ス", NULL},
	{"se", "セ", NULL},
	{"so", "ソ", NULL},

	{"sya", "シャ", NULL},
	{"syi", "シィ", NULL},
	{"syu", "シュ", NULL},
	{"sye", "シェ", NULL},
	{"syo", "ショ", NULL},
    
	{"sha", "シャ", NULL},
	{"shi", "シ", NULL},
	{"shu", "シュ", NULL},
	{"she", "シェ", NULL},
	{"sho", "ショ", NULL},

	{"s", "ッ", "s"},

	{"za", "ザ", NULL},
	{"zi", "ジ", NULL},
	{"zu", "ズ", NULL},
	{"ze", "ゼ", NULL},
	{"zo", "ゾ", NULL},

	{"zya", "ジャ", NULL},
	{"zyi", "ジィ", NULL},
	{"zyu", "ジュ", NULL},
	{"zye", "ジェ", NULL},
	{"zyo", "ジョ", NULL},

	{"z", "ッ", "z"},
    
	{"ja", "ジャ", NULL},
	{"ji", "ジ", NULL},
	{"ju", "ジュ", NULL},
	{"je", "ジェ", NULL},
	{"jo", "ジョ", NULL},

	{"jya", "ジャ", NULL},
	{"jyi", "ジィ", NULL},
	{"jyu", "ジュ", NULL},
	{"jye", "ジェ", NULL},
	{"jyo", "ジョ", NULL},
    
	{"j", "ッ", "j"},
    
	{"ta", "タ", NULL},
	{"ti", "チ", NULL},
	{"tu", "ツ", NULL},
	{"te", "テ", NULL},
	{"to", "ト", NULL},
	{"t.", "t", NULL},  /* 最新版の alt-cannadic の単漢字辞書への対応 */

	{"tya", "チャ", NULL},
	{"tyi", "チィ", NULL},
	{"tyu", "チュ", NULL},
	{"tye", "チェ", NULL},
	{"tyo", "チョ", NULL},

	{"tha", "テァ", NULL},
	{"thi", "ティ", NULL},
	{"thu", "テュ", NULL},
	{"the", "テェ", NULL},
	{"tho", "テョ", NULL},

	{"t", "ッ", "tc"},

	{"cha", "チャ", NULL},
	{"chi", "チ", NULL},
	{"chu", "チュ", NULL},
	{"che", "チェ", NULL},
	{"cho", "チョ", NULL},

	{"tsu", "ツ", NULL},
	{"xtu", "ッ", NULL},
	{"xtsu", "ッ", NULL},

	{"c", "ッ", "c"},

	{"da", "ダ", NULL},
	{"di", "ヂ", NULL},
	{"du", "ヅ", NULL},
	{"de", "デ", NULL},
	{"do", "ド", NULL},

	{"dya", "ヂャ", NULL},
	{"dyi", "ヂィ", NULL},
	{"dyu", "ヂュ", NULL},
	{"dye", "ヂェ", NULL},
	{"dyo", "ヂョ", NULL},

	{"dha", "デャ", NULL},
	{"dhi", "ディ", NULL},
	{"dhu", "デュ", NULL},
	{"dhe", "デェ", NULL},
	{"dho", "デョ", NULL},
    
	{"d", "ッ", "d"},

	{"na", "ナ", NULL},
	{"ni", "ニ", NULL},
	{"nu", "ヌ", NULL},
	{"ne", "ネ", NULL},
	{"no", "ノ", NULL},

	{"nya", "ニャ", NULL},
	{"nyi", "ニィ", NULL},
	{"nyu", "ニュ", NULL},
	{"nye", "ニェ", NULL},
	{"nyo", "ニョ", NULL},

	{"n", "ン", NULL},
	{"nn", "ン", NULL},

	{"ha", "ハ", NULL},
	{"hi", "ヒ", NULL},
	{"hu", "フ", NULL},
	{"he", "ヘ", NULL},
	{"ho", "ホ", NULL},

	{"hya", "ヒャ", NULL},
	{"hyi", "ヒィ", NULL},
	{"hyu", "ヒュ", NULL},
	{"hye", "ヒェ", NULL},
	{"hyo", "ヒョ", NULL},

	{"h", "ッ", "h"},
    
	{"fa", "ファ", NULL},
	{"fi", "フィ", NULL},
	{"fu", "フ", NULL},
	{"fe", "フェ", NULL},
	{"fo", "フォ", NULL},

	{"fya", "フャ", NULL},
	{"fyi", "フィ", NULL},
	{"fyu", "フュ", NULL},
	{"fye", "フェ", NULL},
	{"fyo", "フョ", NULL},

	{"f", "ッ", "f"},
    
	{"ba", "バ", NULL},
	{"bi", "ビ", NULL},
	{"bu", "ブ", NULL},
	{"be", "ベ", NULL},
	{"bo", "ボ", NULL},
    
	{"bya", "ビャ", NULL},
	{"byi", "ビィ", NULL},
	{"byu", "ビュ", NULL},
	{"bye", "ビェ", NULL},
	{"byo", "ビョ", NULL},

	{"b", "ッ", NULL},

	{"pa", "パ", NULL},
	{"pi", "ピ", NULL},
	{"pu", "プ", NULL},
	{"pe", "ペ", NULL},
	{"po", "ポ", NULL},

	{"pya", "ピャ", NULL},
	{"pyi", "ピィ", NULL},
	{"pyu", "ピュ", NULL},
	{"pye", "ピェ", NULL},
	{"pyo", "ピョ", NULL},
    
	{"p", "ッ", "p"},
    
	{"ma", "マ", NULL},
	{"mi", "ミ", NULL},
	{"mu", "ム", NULL},
	{"me", "メ", NULL},
	{"mo", "モ", NULL},

	{"mya", "ミャ", NULL},
	{"myi", "ミィ", NULL},
	{"myu", "ミュ", NULL},
	{"mye", "ミェ", NULL},
	{"myo", "ミョ", NULL},

	{"m", "ン", "bp"},

	{"y", "ッ", "y"},
	{"ya", "ヤ", NULL},
	{"yu", "ユ", NULL},
	{"yo", "ヨ", NULL},

	{"xya", "ャ", NULL},
	{"xyu", "ュ", NULL},
	{"xyo", "ョ", NULL},

	{"r", "ッ", "r"},
	{"ra", "ラ", NULL},
	{"ri", "リ", NULL},
	{"ru", "ル", NULL},
	{"re", "レ", NULL},
	{"ro", "ロ", NULL},

	{"rya", "リャ", NULL},
	{"ryi", "リィ", NULL},
	{"ryu", "リュ", NULL},
	{"rye", "リェ", NULL},
	{"ryo", "リョ", NULL},

	{"xwa", "ヮ", NULL},
	{"wa", "ワ", NULL},
	{"wi", "ウィ", NULL},
	{"xwi", "ヰ", NULL},
	{"we", "ウェ", NULL},
	{"xwe", "ヱ", NULL},
	{"wo", "ヲ", NULL},
    
	{"va", "ヴァ", NULL},
	{"vi", "ヴィ", NULL},
	{"vu", "ヴ", NULL},
	{"ve", "ヴェ", NULL},
	{"vo", "ヴォ", NULL},

	{NULL, NULL, NULL}
};

static const struct rk_rule rk_rule_hankaku_kana[] =
{
	SKK_LIKE_KIGO_MAP,

	{"a", "ｱ", NULL},
	{"i", "ｲ", NULL},
	{"u", "ｳ", NULL},
	{"e", "ｴ", NULL},
	{"o", "ｵ", NULL},

	{"xa", "ｧ", NULL},
	{"xi", "ｨ", NULL},
	{"xu", "ｩ", NULL},
	{"xe", "ｪ", NULL},
	{"xo", "ｫ", NULL},
	
	{"ka", "ｶ", NULL},
	{"ki", "ｷ", NULL},
	{"ku", "ｸ", NULL},
	{"ke", "ｹ", NULL},
	{"ko", "ｺ", NULL},

	{"kya", "ｷｬ", NULL},
	{"kyi", "kｲ", NULL},
	{"kyu", "ｷｭ", NULL},
	{"kye", "ｷｪ", NULL},
	{"kyo", "ｷｮ", NULL},
    
	{"k", "ｯ", "k"},

	{"ga", "ｶﾞ", NULL},
	{"gi", "ｷﾞ", NULL},
	{"gu", "ｸﾞ", NULL},
	{"ge", "ｹﾞ", NULL},
	{"go", "ｺﾞ", NULL},

	{"gya", "ｷﾞｬ", NULL},
	{"gyi", "ｷﾞｨ", NULL},
	{"gyu", "ｷﾞｭ", NULL},
	{"gye", "ｷﾞｪ", NULL},
	{"gyo", "ｷﾞｮ", NULL},
    
	{"g", "ｯ", "g"},

	{"sa", "ｻ", NULL},
	{"si", "ｼ", NULL},
	{"su", "ｽ", NULL},
	{"se", "ｾ", NULL},
	{"so", "ｿ", NULL},

	{"sya", "ｼｬ", NULL},
	{"syi", "ｼｨ", NULL},
	{"syu", "ｼｭ", NULL},
	{"sye", "ｼｪ", NULL},
	{"syo", "ｼｮ", NULL},
    
	{"sha", "ｼｬ", NULL},
	{"shi", "ｼ", NULL},
	{"shu", "ｼｭ", NULL},
	{"she", "ｼｪ", NULL},
	{"sho", "ｼｮ", NULL},

	{"s", "ｯ", "s"},

	{"za", "ｻﾞ", NULL},
	{"zi", "ｼﾞ", NULL},
	{"zu", "ｽﾞ", NULL},
	{"ze", "ｾﾞ", NULL},
	{"zo", "ｿﾞ", NULL},

	{"zya", "ｼﾞｬ", NULL},
	{"zyi", "ｼﾞｨ", NULL},
	{"zyu", "ｼﾞｭ", NULL},
	{"zye", "ｼﾞｪ", NULL},
	{"zyo", "ｼﾞｮ", NULL},

	{"z", "ｯ", "z"},
    
	{"ja", "ｼﾞｬ", NULL},
	{"ji", "ｼﾞ", NULL},
	{"ju", "ｼﾞｭ", NULL},
	{"je", "ｼﾞｪ", NULL},
	{"jo", "ｼﾞｮ", NULL},

	{"jya", "ｼﾞｬ", NULL},
	{"jyi", "ｼﾞｨ", NULL},
	{"jyu", "ｼﾞｭ", NULL},
	{"jye", "ｼﾞｪ", NULL},
	{"jyo", "ｼﾞｮ", NULL},
    
	{"j", "ｯ", "j"},
    
	{"ta", "ﾀ", NULL},
	{"ti", "ﾁ", NULL},
	{"tu", "ﾂ", NULL},
	{"te", "ﾃ", NULL},
	{"to", "ﾄ", NULL},
	{"t.", "t", NULL},  /* 最新版の alt-cannadic の単漢字辞書への対応 */

	{"tya", "ﾁｬ", NULL},
	{"tyi", "ﾁｨ", NULL},
	{"tyu", "ﾁｭ", NULL},
	{"tye", "ﾁｪ", NULL},
	{"tyo", "ﾁｮ", NULL},

	{"tha", "ﾃｧ", NULL},
	{"thi", "ﾃｨ", NULL},
	{"thu", "ﾁｭ", NULL},
	{"the", "ﾁｪ", NULL},
	{"tho", "ﾁｮ", NULL},

	{"t", "ｯ", "tc"},

	{"cha", "ﾁｬ", NULL},
	{"chi", "ﾁ", NULL},
	{"chu", "ﾁｭ", NULL},
	{"che", "ﾁｪ", NULL},
	{"cho", "ﾁｮ", NULL},

	{"tsu", "ﾂ", NULL},
	{"xtu", "ｯ", NULL},
	{"xtsu", "ｯ", NULL},

	{"c", "ｯ", "c"},

	{"da", "ﾀﾞ", NULL},
	{"di", "ﾁﾞ", NULL},
	{"du", "ﾂﾞ", NULL},
	{"de", "ﾃﾞ", NULL},
	{"do", "ﾄﾞ", NULL},

	{"dya", "ﾁﾞｬ", NULL},
	{"dyi", "ﾁﾞｨ", NULL},
	{"dyu", "ﾁﾞｩ", NULL},
	{"dye", "ﾁﾞｪ", NULL},
	{"dyo", "ﾁﾞｮ", NULL},

	{"dha", "ﾃﾞｬ", NULL},
	{"dhi", "ﾃﾞｨ", NULL},
	{"dhu", "ﾃﾞｭ", NULL},
	{"dhe", "ﾃﾞｪ", NULL},
	{"dho", "ﾃﾞｮ", NULL},
    
	{"d", "ｯ", "d"},

	{"na", "ﾅ", NULL},
	{"ni", "ﾆ", NULL},
	{"nu", "ﾇ", NULL},
	{"ne", "ﾈ", NULL},
	{"no", "ﾉ", NULL},

	{"nya", "ﾆｬ", NULL},
	{"nyi", "ﾆｨ", NULL},
	{"nyu", "ﾆｭ", NULL},
	{"nye", "ﾆｪ", NULL},
	{"nyo", "ﾆｮ", NULL},

	{"n", "ﾝ", NULL},
	{"nn", "ﾝ", NULL},

	{"ha", "ﾊ", NULL},
	{"hi", "ﾋ", NULL},
	{"hu", "ﾌ", NULL},
	{"he", "ﾍ", NULL},
	{"ho", "ﾎ", NULL},

	{"hya", "ﾋｬ", NULL},
	{"hyi", "ﾋｨ", NULL},
	{"hyu", "ﾋｭ", NULL},
	{"hye", "ﾋｪ", NULL},
	{"hyo", "ﾋｮ", NULL},

	{"h", "ｯ", "h"},
    
	{"fa", "ﾌｧ", NULL},
	{"fi", "ﾌｨ", NULL},
	{"fu", "ﾌ", NULL},
	{"fe", "ﾌｪ", NULL},
	{"fo", "ﾌｫ", NULL},

	{"fya", "ﾌｬ", NULL},
	{"fyi", "ﾌｨ", NULL},
	{"fyu", "ﾌｭ", NULL},
	{"fye", "ﾌｪ", NULL},
	{"fyo", "ﾌｮ", NULL},

	{"f", "ｯ", "f"},
    
	{"ba", "ﾊﾞ", NULL},
	{"bi", "ﾋﾞ", NULL},
	{"bu", "ﾌﾞ", NULL},
	{"be", "ﾍﾞ", NULL},
	{"bo", "ﾎﾞ", NULL},
    
	{"bya", "ﾋﾞｬ", NULL},
	{"byi", "ﾋﾞｨ", NULL},
	{"byu", "ﾋﾞｭ", NULL},
	{"bye", "ﾋﾞｪ", NULL},
	{"byo", "ﾋﾞｮ", NULL},

	{"b", "ｯ", NULL},

	{"pa", "ﾊﾟ", NULL},
	{"pi", "ﾋﾟ", NULL},
	{"pu", "ﾌﾟ", NULL},
	{"pe", "ﾍﾟ", NULL},
	{"po", "ﾎﾟ", NULL},

	{"pya", "ﾋﾟｬ", NULL},
	{"pyi", "ﾋﾟｨ", NULL},
	{"pyu", "ﾋﾟｭ", NULL},
	{"pye", "ﾋﾟｪ", NULL},
	{"pyo", "ﾋﾟｮ", NULL},
    
	{"p", "ｯ", "p"},
    
	{"ma", "ﾏ", NULL},
	{"mi", "ﾐ", NULL},
	{"mu", "ﾑ", NULL},
	{"me", "ﾒ", NULL},
	{"mo", "ﾓ", NULL},

	{"mya", "ﾐｬ", NULL},
	{"myi", "ﾐｨ", NULL},
	{"myu", "ﾐｭ", NULL},
	{"mye", "ﾐｪ", NULL},
	{"myo", "ﾐｮ", NULL},

	{"m", "ﾝ", "bp"},

	{"y", "ｯ", "y"},
	{"ya", "ﾔ", NULL},
	{"yu", "ﾕ", NULL},
	{"yo", "ﾖ", NULL},

	{"xya", "ｬ", NULL},
	{"xyu", "ｭ", NULL},
	{"xyo", "ｮ", NULL},

	{"r", "ｯ", "r"},
	{"ra", "ﾗ", NULL},
	{"ri", "ﾘ", NULL},
	{"ru", "ﾙ", NULL},
	{"re", "ﾚ", NULL},
	{"ro", "ﾛ", NULL},

	{"rya", "ﾘｬ", NULL},
	{"ryi", "ﾘｨ", NULL},
	{"ryu", "ﾘｭ", NULL},
	{"rye", "ﾘｪ", NULL},
	{"ryo", "ﾘｮ", NULL},

	{"xwa", "ﾜ", NULL},
	{"wa", "ﾜ", NULL},
	{"wi", "ｳｨ", NULL},
	{"xwi", "ｳｨ", NULL},
	{"we", "ｳｪ", NULL},
	{"xwe", "ｳｪ", NULL},
	{"wo", "ｦ", NULL},
    
	{"va", "ｳﾞｧ", NULL},
	{"vi", "ｳﾞｨ", NULL},
	{"vu", "ｳﾞ", NULL},
	{"ve", "ｳﾞｪ", NULL},
	{"vo", "ｳﾞｫ", NULL},

	{NULL, NULL, NULL}
};

/*
 * Local variables:
 *  c-indent-level: 8
 *  c-basic-offset: 8
 * End:
 */
