package org.fcitx.fcitx5.android.input.handwriting

import com.google.mlkit.common.model.DownloadConditions
import com.google.mlkit.common.model.RemoteModelManager
import com.google.mlkit.vision.digitalink.DigitalInkRecognition
import com.google.mlkit.vision.digitalink.DigitalInkRecognitionModel
import com.google.mlkit.vision.digitalink.DigitalInkRecognitionModelIdentifier
import com.google.mlkit.vision.digitalink.DigitalInkRecognizer
import com.google.mlkit.vision.digitalink.DigitalInkRecognizerOptions
import com.google.mlkit.vision.digitalink.Ink
import kotlinx.coroutines.tasks.await
import timber.log.Timber

enum class EngineState {
    NotReady,
    Downloading,
    Ready,
    Error
}

class HandwritingEngine {
    
    var state: EngineState = EngineState.NotReady
        private set
    
    private var recognizer: DigitalInkRecognizer? = null
    
    fun initModel() {
        if (state == EngineState.Ready || state == EngineState.Downloading) {
            return
        }

        val modelIdentifier = try {
            DigitalInkRecognitionModelIdentifier.fromLanguageTag("zh-CN")
        } catch (e: Exception) {
            Timber.e(e, "Language tag not found")
            state = EngineState.Error
            return
        }
        
        if (modelIdentifier == null) {
            Timber.e("Model Identifier is null")
            state = EngineState.Error
            return
        }

        val model = DigitalInkRecognitionModel.builder(modelIdentifier).build()
        val remoteModelManager = RemoteModelManager.getInstance()
        
        state = EngineState.Downloading
        Timber.i("Start downloading ML Kit Digital Ink model for zh-CN")
        
        remoteModelManager.download(model, DownloadConditions.Builder().build())
            .addOnSuccessListener {
                Timber.i("Model downloaded successfully")
                state = EngineState.Ready
                val options = DigitalInkRecognizerOptions.builder(model).build()
                recognizer = DigitalInkRecognition.getClient(options)
            }
            .addOnFailureListener { e ->
                Timber.e(e, "Error while downloading a model")
                state = EngineState.Error
            }
    }
    
    /**
     * @return recognized string list or empty
     */
    suspend fun recognize(ink: Ink): List<String> {
        val currentRecognizer = recognizer
        if (state != EngineState.Ready || currentRecognizer == null) {
            Timber.w("Handwriting engine is not ready (current state: \$state)")
            return emptyList()
        }
        
        return try {
            val result = currentRecognizer.recognize(ink).await()
            result.candidates.map { it.text }
        } catch (e: Exception) {
            Timber.e(e, "Recognition failed")
            emptyList()
        }
    }
    
    fun close() {
        recognizer?.close()
        recognizer = null
        state = EngineState.NotReady
    }
    
}
