package com.example.moereng.utils.text

import android.content.res.AssetManager

class ChineseTextUtils(
    override val symbols: List<String>,
    override val cleanerName: String,
    override val assetManager: AssetManager
) : BaseTextUtils(symbols, cleanerName, assetManager) {

    private val cleaner = ChineseCleaners()

    override fun wordsToLabels(text: String): IntArray {
        val labels = ArrayList<Int>()
        labels.add(0)

        // symbol to id
        val symbolToIndex = HashMap<String, Int>()
        symbols.forEachIndexed { index, s ->
            symbolToIndex[s] = index
        }

        // clean text
        var cleanedText = ""

        when (cleanerName){
            "chinese_cleaners" -> {
                cleanedText = cleaner.chinese_clean_text(text)
            }

            "chinese_cleaners1" -> {
                cleanedText = cleaner.chinese_clean_text(text)
            }

            "chinese_cleaners_moegoe" -> {
                cleanedText = cleaner.chinese_clean_text_moegoe(text)
            }
        }

        if (cleanedText.isEmpty()){
            throw RuntimeException("转换失败，请检查输入！")
        }

        // symbol to label
        for (symbol in cleanedText) {
            if (!symbols.contains(symbol.toString())) {
                continue
            }
            val label = symbolToIndex[symbol.toString()]
            if (label != null) {
                labels.add(label)
                labels.add(0)
            }
        }
        return labels.toIntArray()
    }
}

