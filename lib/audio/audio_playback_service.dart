import 'dart:async';
import 'dart:io';
import 'package:flutter/foundation.dart';

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
    if (!file.existsSync()) {
      // In simulation or test mode
      _duration = const Duration(seconds: 3);
    } else {
      final bytes = file.lengthSync();
      // 48 kHz 16-bit mono = 96,000 bytes per second
      final seconds = bytes / 96000.0;
      _duration = Duration(milliseconds: (seconds * 1000).toInt());
    }

    _currentFilePath = filePath;
    _position = Duration.zero;
    _state = AudioPlaybackState.playing;
    notifyListeners();

    _ticker?.cancel();
    _ticker = Timer.periodic(const Duration(milliseconds: 100), (t) {
      if (_state == AudioPlaybackState.playing) {
        _position += const Duration(milliseconds: 100);
        if (_position >= _duration) {
          stop();
        } else {
          notifyListeners();
        }
      }
    });
  }

  void pause() {
    if (_state == AudioPlaybackState.playing) {
      _state = AudioPlaybackState.paused;
      notifyListeners();
    }
  }

  void resume() {
    if (_state == AudioPlaybackState.paused) {
      _state = AudioPlaybackState.playing;
      notifyListeners();
    }
  }

  void seek(Duration newPosition) {
    _position = newPosition;
    if (_position > _duration) _position = _duration;
    notifyListeners();
  }

  void stop() {
    _ticker?.cancel();
    _state = AudioPlaybackState.stopped;
    _position = Duration.zero;
    notifyListeners();
  }
}
