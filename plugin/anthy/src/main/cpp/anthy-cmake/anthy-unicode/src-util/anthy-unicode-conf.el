;; anthy-unicode-conf.el -- Anthy


;; Copyright (C) 2002
;; Author: Yusuke Tabata<yusuke@kmc.gr.jp>

;; This file is part of Anthy

;;; Commentary:
;;

(defvar anthy-alt-char-map
  '(("," "，")
    ("." "．")))

(defvar anthy-kana-mode-hiragana-map
  '(
    ("3" . "あ")    ("e" . "い")    ("4" . "う")    ("5" . "え")    ("6" . "お")
    ("#" . "ぁ")    ("E" . "ぃ")    ("$" . "ぅ")    ("%" . "ぇ")    ("&" . "ぉ")
    ("t" . "か")    ("g" . "き")    ("h" . "く")    (":" . "け")    ("b" . "こ")
    ("t@" . "が")    ("g@" . "ぎ")    ("h@" . "ぐ")    (":@" . "げ")    ("b@" . "ご")
    ("x" . "さ")    ("d" . "し")    ("r" . "す")    ("p" . "せ")    ("c" . "そ")
    ("x@" . "ざ")    ("d@" . "じ")    ("r@" . "ず")    ("p@" . "ぜ")    ("c@" . "ぞ")
    ("q" . "た")    ("a" . "ち")    ("z" . "つ")    ("w" . "て")    ("s" . "と")
    ("q@" . "だ")    ("a@" . "ぢ")    ("z@" . "づ")    ("w@" . "で")    ("s@" . "ど")
    ("u" . "な")    ("i" . "に")    ("1" . "ぬ")    ("," . "ね")    ("k" . "の")
    ("f" . "は")    ("v" . "ひ")    ("2" . "ふ")    ("^" . "へ")    ("-" . "ほ")
    ("f@" . "ば")    ("v@" . "び")    ("2@" . "ぶ")    ("^@" . "べ")    ("-@" . "ぼ")
    ("f[" . "ぱ")    ("v[" . "ぴ")    ("2[" . "ぷ")    ("^[" . "ぺ")    ("-[" . "ぽ")
    ("j" . "ま")    ("n" . "み")    ("]" . "む")    ("/" . "め")    ("m" . "も")
    ("7" . "や")    ("8" . "ゆ")    ("9" . "よ")
    ("'" . "ゃ")    ("(" . "ゅ")    (")" . "ょ")
    ("o" . "ら")    ("l" . "り")    ("." . "る")    (";" . "れ")    ("\\" . "ろ")
    ("0" . "わ")    ("~" . "を")

    ;; 困ったことに バックスラッシュと円を区別する方法がわからない
    ("|" . "ー")    ("_" . "ー")    ("<" . "、")    (">" . "。")
    ("Z" . "っ")    ("y" . "ん")
    ))

(defvar anthy-kana-mode-katakana-map
  '(
    ("3" . "ア")    ("e" . "イ")    ("4" . "ウ")    ("5" . "エ")    ("6" . "オ")
    ("#" . "ァ")    ("E" . "ィ")    ("$" . "ゥ")    ("%" . "ェ")    ("&" . "ォ")
    ("t" . "カ")    ("g" . "キ")    ("h" . "ク")    (":" . "ケ")    ("b" . "コ")
    ("t@" . "ガ")    ("g@" . "ギ")    ("h@" . "グ")    (":@" . "ゲ")    ("b@" . "ゴ")
    ("x" . "サ")    ("d" . "シ")    ("r" . "ス")    ("p" . "セ")    ("c" . "ソ")
    ("x@" . "ザ")    ("d@" . "ジ")    ("r@" . "ズ")    ("p@" . "ゼ")    ("c@" . "ゾ")
    ("q" . "タ")    ("a" . "チ")    ("z" . "ツ")    ("w" . "テ")    ("s" . "ト")
    ("q@" . "ダ")    ("a@" . "ヂ")    ("z@" . "ヅ")    ("w@" . "デ")    ("s@" . "ド")
    ("u" . "ナ")    ("i" . "ニ")    ("1" . "ヌ")    ("," . "ネ")    ("k" . "ノ")
    ("f" . "ハ")    ("v" . "ヒ")    ("2" . "フ")    ("^" . "ヘ")    ("-" . "ホ")
    ("f@" . "バ")    ("v@" . "ビ")    ("2@" . "ブ")    ("^@" . "ベ")    ("-@" . "ボ")
    ("f[" . "パ")    ("v[" . "ピ")    ("2[" . "プ")    ("^[" . "ペ")    ("-[" . "ポ")
    ("j" . "マ")    ("n" . "ミ")    ("]" . "ム")    ("/" . "メ")    ("m" . "モ")
    ("7" . "ヤ")    ("8" . "ユ")    ("9" . "ヨ")
    ("'" . "ャ")    ("(" . "ュ")    (")" . "ョ")
    ("o" . "ラ")    ("l" . "リ")    ("." . "ル")    (";" . "レ")    ("\\" . "ロ")
    ("0" . "ワ")    ("~" . "ヲ")

    ;; 困ったことに バックスラッシュと円を区別する方法がわからない
    ("|" . "ー")    ("_" . "ー")    ("<" . "、")    (">" . "。")
    ("Z" . "ッ")    ("y" . "ン")
    ))

;;
;; mapの変更
;;
(defun anthy-send-map-edit-command (mapno key str)
  (if (not (stringp key))
      (setq key (char-to-string key)))
  (if (not (stringp str))
      (setq str (char-to-string str)))
  (anthy-send-recv-command
   (concat " MAP_EDIT " (int-to-string mapno)
	   " " key " " str "\n")))
(defun anthy-change-hiragana-map (key str)
  (anthy-send-map-edit-command 2 key str))
(defun anthy-change-katakana-map (key str)
  (anthy-send-map-edit-command 3 key str))
(defun anthy-load-hiragana-map (map)
  (mapcar (lambda (x)
	    (let ((key (car x))
		  (str (cadr x)))
	      (anthy-change-hiragana-map key str))) map))
(defun anthy-clear-map ()
  (anthy-send-recv-command
   " MAP_CLEAR 0\n"))
;;
;; toggleの変更
;;
(defun anthy-send-change-toggle-command (str)
  (anthy-send-recv-command
   (concat " CHANGE_TOGGLE " str "\n")))

;; should disable toggle
;; (ローマ字ではなくて)かなモードにする
(defun anthy-kana-map-mode ()
  (setq anthy-rkmap-keybind
	'(
	  ;; \C-p
	  (("hiragana" . 16) . "katakana")
	  (("katakana" . 16) . "hiragana")))
  (define-key anthy-mode-map (char-to-string 16) 'anthy-insert)
  (anthy-send-recv-command " SET_PREEDIT_MODE 1\n")
  (anthy-send-change-toggle-command "!")
  (anthy-clear-map)
  (mapcar (lambda (x)
	    (anthy-change-hiragana-map (car x) (cdr x)))
	  anthy-kana-mode-hiragana-map)
  (mapcar (lambda (x)
	    (anthy-change-katakana-map (car x) (cdr x)))
	  anthy-kana-mode-katakana-map))
;;
(defun anthy-set-break-into-roman (flag)
  "読みを入力中にバックスペースを打つとローマ字までバラバラにする"
  (anthy-send-recv-command
   (if flag
       " BREAK_INTO_ROMAN 1\n"
     " BREAK_INTO_ROMAN 0\n")))

(provide 'anthy-unicode-conf)
