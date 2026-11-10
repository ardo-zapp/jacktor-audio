package com.jacktor.audiopanel.service

import android.app.Notification
import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.drawable.BitmapDrawable
import android.graphics.drawable.Drawable
import android.graphics.drawable.Icon
import android.media.MediaMetadata
import android.media.session.MediaController
import android.media.session.MediaSession
import android.media.session.PlaybackState
import android.os.Build
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification
import android.util.Log
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.io.File
import java.io.FileOutputStream

data class MediaTrackInfo(
    val title: String = "",
    val artist: String = "",
    val album: String = "",
    val packageName: String = "",
    val isPlaying: Boolean = false,
    val hasArtwork: Boolean = false,
    val timestamp: Long = System.currentTimeMillis()
)

class MediaNotificationListenerService : NotificationListenerService() {

    private val TAG = "JacktorMediaListener"

    companion object {
        private val _mediaState = MutableStateFlow(MediaTrackInfo())
        val mediaState: StateFlow<MediaTrackInfo> = _mediaState.asStateFlow()

        var currentBitmap: Bitmap? = null
            private set

        private var activeController: MediaController? = null

        fun sendTransportControl(action: String): Boolean {
            val ctrl = activeController ?: return false
            return try {
                when (action.lowercase()) {
                    "play" -> {
                        ctrl.transportControls.play()
                        _mediaState.value = _mediaState.value.copy(isPlaying = true)
                        true
                    }
                    "pause" -> {
                        ctrl.transportControls.pause()
                        _mediaState.value = _mediaState.value.copy(isPlaying = false)
                        true
                    }
                    "next" -> {
                        ctrl.transportControls.skipToNext()
                        true
                    }
                    "prev" -> {
                        ctrl.transportControls.skipToPrevious()
                        true
                    }
                    else -> false
                }
            } catch (e: Exception) {
                Log.w("JacktorMediaListener", "Failed to send transport control $action: ${e.message}")
                false
            }
        }

        fun getCover(context: Context): Bitmap? {
            if (currentBitmap != null) return currentBitmap
            try {
                val f = File(context.filesDir, "cover.jpg")
                if (f.exists()) {
                    currentBitmap = BitmapFactory.decodeFile(f.absolutePath)
                    return currentBitmap
                }
                val tmpF = File("/data/local/tmp/current_cover.jpg")
                if (tmpF.exists()) {
                    currentBitmap = BitmapFactory.decodeFile(tmpF.absolutePath)
                    return currentBitmap
                }
            } catch (_: Exception) {}
            return null
        }
    }

    override fun onListenerConnected() {
        super.onListenerConnected()
        Log.i(TAG, "Notification listener connected, scanning active notifications...")
        try {
            val activeNotifs = activeNotifications
            if (activeNotifs != null) {
                for (sbn in activeNotifs) {
                    processMediaNotification(sbn)
                }
            }
        } catch (e: Exception) {
            Log.w(TAG, "Error checking active notifications: ${e.message}")
        }
    }

    override fun onNotificationPosted(sbn: StatusBarNotification?) {
        super.onNotificationPosted(sbn)
        processMediaNotification(sbn)
    }

    private fun processMediaNotification(sbn: StatusBarNotification?) {
        val notif = sbn?.notification ?: return
        val extras = notif.extras ?: return

        // 1. Try extracting direct MediaSession.Token
        val sessionToken = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            extras.getParcelable(Notification.EXTRA_MEDIA_SESSION, MediaSession.Token::class.java)
        } else {
            @Suppress("DEPRECATION")
            extras.getParcelable(Notification.EXTRA_MEDIA_SESSION) as? MediaSession.Token
        }

        val title = extras.getCharSequence(Notification.EXTRA_TITLE)?.toString() ?: ""
        val artist = extras.getCharSequence(Notification.EXTRA_TEXT)?.toString() ?: ""

        if (sessionToken != null) {
            try {
                val controller = MediaController(this, sessionToken)
                activeController = controller
                val pbState = controller.playbackState
                val isPlaying = pbState?.state == PlaybackState.STATE_PLAYING
                val meta = controller.metadata

                val metaTitle = meta?.getString(MediaMetadata.METADATA_KEY_TITLE)
                val metaArtist = meta?.getString(MediaMetadata.METADATA_KEY_ARTIST)
                val metaAlbum = meta?.getString(MediaMetadata.METADATA_KEY_ALBUM)
                val metaBitmap = meta?.getBitmap(MediaMetadata.METADATA_KEY_ALBUM_ART)
                    ?: meta?.getBitmap(MediaMetadata.METADATA_KEY_ART)

                val effectiveTitle = if (!metaTitle.isNullOrBlank()) metaTitle else title
                val effectiveArtist = if (!metaArtist.isNullOrBlank()) metaArtist else artist
                val effectiveAlbum = metaAlbum ?: ""

                if (metaBitmap != null) {
                    currentBitmap = metaBitmap
                    saveArtworkBitmap(metaBitmap)
                } else {
                    extractNotificationIcon(notif, extras)
                }

                if (effectiveTitle.isNotBlank() || effectiveArtist.isNotBlank()) {
                    _mediaState.value = MediaTrackInfo(
                        title = effectiveTitle,
                        artist = effectiveArtist,
                        album = effectiveAlbum,
                        packageName = sbn.packageName,
                        isPlaying = isPlaying,
                        hasArtwork = currentBitmap != null
                    )
                    Log.d(TAG, "MediaSession active: $effectiveTitle - $effectiveArtist ($isPlaying)")
                    return
                }
            } catch (e: Exception) {
                Log.w(TAG, "MediaController initialization error: ${e.message}")
            }
        }

        // 2. Fallback: Check standard notification categories & extras
        val isMediaStyle = extras.getString(Notification.EXTRA_TEMPLATE)?.contains("MediaStyle") == true
        val hasMediaSession = extras.containsKey(Notification.EXTRA_MEDIA_SESSION)
        val isTransport = notif.category == Notification.CATEGORY_TRANSPORT

        if (isMediaStyle || hasMediaSession || isTransport) {
            extractNotificationIcon(notif, extras)

            if (title.isNotBlank() || artist.isNotBlank()) {
                _mediaState.value = MediaTrackInfo(
                    title = title,
                    artist = artist,
                    album = "",
                    packageName = sbn.packageName,
                    isPlaying = true,
                    hasArtwork = currentBitmap != null
                )
                Log.d(TAG, "Media notification processed: $title - $artist (${sbn.packageName})")
            }
        }
    }

    private fun extractNotificationIcon(notif: Notification, extras: android.os.Bundle) {
        val directBitmap = (extras.get(Notification.EXTRA_LARGE_ICON) as? Bitmap)
            ?: (extras.get(Notification.EXTRA_PICTURE) as? Bitmap)

        if (directBitmap != null) {
            currentBitmap = directBitmap
            saveArtworkBitmap(directBitmap)
            return
        }

        val largeIcon: Icon? = notif.getLargeIcon() ?: (extras.get(Notification.EXTRA_LARGE_ICON) as? Icon)
        if (largeIcon != null) {
            try {
                val drawable = largeIcon.loadDrawable(this)
                val bitmap = drawableToBitmap(drawable)
                if (bitmap != null) {
                    currentBitmap = bitmap
                    saveArtworkBitmap(bitmap)
                }
            } catch (e: Exception) {
                Log.w(TAG, "Failed to extract media artwork from Icon: ${e.message}")
            }
        }
    }

    override fun onNotificationRemoved(sbn: StatusBarNotification?) {
        super.onNotificationRemoved(sbn)
        val pkg = sbn?.packageName ?: return
        if (pkg == _mediaState.value.packageName) {
            Log.d(TAG, "Media notification removed for $pkg")
            _mediaState.value = _mediaState.value.copy(isPlaying = false)
        }
    }

    private fun drawableToBitmap(drawable: Drawable?): Bitmap? {
        if (drawable == null) return null
        if (drawable is BitmapDrawable) {
            return drawable.bitmap
        }
        val width = if (drawable.intrinsicWidth > 0) drawable.intrinsicWidth else 512
        val height = if (drawable.intrinsicHeight > 0) drawable.intrinsicHeight else 512
        val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bitmap)
        drawable.setBounds(0, 0, canvas.width, canvas.height)
        drawable.draw(canvas)
        return bitmap
    }

    private fun saveArtworkBitmap(bitmap: Bitmap) {
        // 1. Save to app internal files directory (always writable by this app)
        try {
            val internalFile = File(filesDir, "cover.jpg")
            FileOutputStream(internalFile).use { fos ->
                bitmap.compress(Bitmap.CompressFormat.JPEG, 90, fos)
                fos.flush()
            }
            internalFile.setReadable(true, false)
        } catch (e: Exception) {
            Log.w(TAG, "Could not save artwork to internal filesDir: ${e.message}")
        }

        // 2. Also save to shared /data/local/tmp/current_cover.jpg for system-wide access
        try {
            val targetFile = File("/data/local/tmp/current_cover.jpg")
            FileOutputStream(targetFile).use { fos ->
                bitmap.compress(Bitmap.CompressFormat.JPEG, 90, fos)
                fos.flush()
            }
            targetFile.setReadable(true, false)
            targetFile.setWritable(true, false)
        } catch (_: Exception) {
            // Non-fatal, app internal cover.jpg is already saved
        }
    }
}
