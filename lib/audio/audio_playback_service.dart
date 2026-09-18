import 'dart:async';
import 'dart:io';
import 'package:flutter/foundation.dart';
import '../core/ffi_bindings.dart';

enum AudioPlaybackState {
  stopped,
  playing,
  paused;
}

class AudioPlaybackService extends ChangeNotifier {
  static final AudioPlaybackService instance = AudioPlaybackService._internal();

  AudioPlaybackState _state = AudioPlaybackState.stopped;
  String? _currentFilePath;
  Duration _position = Duration.zero;
  Duration _duration = Duration.zero;
  Timer? _ticker;

  AudioPlaybackService._internal();

  AudioPlaybackState get state => _state;
  String? get currentFilePath => _currentFilePath;
  Duration get position => _position;
  Duration get duration => _duration;
  bool get isPlaying => _state == AudioPlaybackState.playing;

  Future<void> play(String filePath) async {
    final file = File(filePath);
    if (file.existsSync()) {
      final bytes = file.lengthSync();
      // 48 kHz 16-bit mono = 96,000 bytes per second (subtract 44 bytes header)
      final dataBytes = bytes > 44 ? bytes - 44 : bytes;
      final seconds = dataBytes / 96000.0;
      _duration = Duration(milliseconds: (seconds * 1000).toInt());
    } else {
      _duration = const Duration(seconds: 3);
    }

    _currentFilePath = filePath;
    _position = Duration.zero;
    _state = AudioPlaybackState.playing;
    notifyListeners();

    // Trigger physical playback through native AudioHAL
    try {
      PolyglotNativeBindings.instance.playAudioFile(filePath);
      final nativeDur = PolyglotNativeBindings.instance.getAudioPlaybackDuration();
      if (nativeDur > 0) {
        _duration = Duration(milliseconds: (nativeDur * 1000).toInt());
      }
    } catch (e) {
      // In tests or headless environments without native library
      debugPrint('Native audio playback unavailable: $e');
    }

    _ticker?.cancel();
    _ticker = Timer.periodic(const Duration(milliseconds: 50), (t) {
      if (_state == AudioPlaybackState.playing) {
        try {
          if (!PolyglotNativeBindings.instance.isAudioPlaying()) {
            stop();
            return;
          }
          final posSec = PolyglotNativeBindings.instance.getAudioPlaybackPosition();
          _position = Duration(milliseconds: (posSec * 1000).toInt());
        } catch (_) {
          _position += const Duration(milliseconds: 50);
          if (_position >= _duration) {
            stop();
            return;
          }
        }
        notifyListeners();
      }
    });
  }

  void pause() {
    if (_state == AudioPlaybackState.playing) {
      _state = AudioPlaybackState.paused;
      try {
        PolyglotNativeBindings.instance.pauseAudioPlayback();
      } catch (_) {}
      notifyListeners();
    }
  }

  void resume() {
    if (_state == AudioPlaybackState.paused) {
      _state = AudioPlaybackState.playing;
      try {
        PolyglotNativeBindings.instance.resumeAudioPlayback();
      } catch (_) {}
      notifyListeners();
    }
  }

  void seek(Duration newPosition) {
    _position = newPosition;
    if (_position > _duration) _position = _duration;
    try {
      PolyglotNativeBindings.instance.seekAudioPlayback(_position.inMilliseconds / 1000.0);
    } catch (_) {}
    notifyListeners();
  }

  void stop() {
    _ticker?.cancel();
    _state = AudioPlaybackState.stopped;
    _position = Duration.zero;
    try {
      PolyglotNativeBindings.instance.stopAudioPlayback();
    } catch (_) {}
    notifyListeners();
  }
}
