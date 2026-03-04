;; -*- no-byte-compile: t -*-
(register-input-method "japanese-anthy-unicode" "Japanese"
		       'anthy-unicode-leim-activate "[anthy-unicode]"
		       "Anthy Unicode Kana Kanji conversion system")

(autoload 'anthy-unicode-leim-activate "anthy-unicode")
;;
