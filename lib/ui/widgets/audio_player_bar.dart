import 'dart:io';
import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:path/path.dart' as p;
import '../../audio/audio_playback_service.dart';

class AudioPlayerBar extends StatelessWidget {
  final String audioFilePath;
  final int durationMs;

  const AudioPlayerBar({
    super.key,
    required this.audioFilePath,
    required this.durationMs,
  });

  @override
  Widget build(BuildContext context) {
    final player = AudioPlaybackService.instance;

    return AnimatedBuilder(
      animation: player,
      builder: (context, _) {
        final isThisPlaying =
            player.isPlaying && player.currentFilePath == audioFilePath;

        return Container(
          padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
          decoration: BoxDecoration(
            color: Colors.black.withOpacity(0.35),
            borderRadius: BorderRadius.circular(8),
            border: Border.all(color: Colors.white12),
          ),
          child: Row(
            children: [
              IconButton(
                iconSize: 22,
                padding: EdgeInsets.zero,
                constraints: const BoxConstraints(),
                icon: Icon(
                  isThisPlaying ? Icons.pause_circle_filled : Icons.play_circle_fill,
                  color: Colors.cyanAccent,
                ),
                onPressed: () {
                  if (isThisPlaying) {
                    player.pause();
                  } else {
                    player.play(audioFilePath);
                  }
                },
              ),
              const SizedBox(width: 8),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    LinearProgressIndicator(
                      value: isThisPlaying && player.duration.inMilliseconds > 0
                          ? player.position.inMilliseconds /
                              player.duration.inMilliseconds
                          : 0.0,
                      backgroundColor: Colors.white10,
                      valueColor:
                          const AlwaysStoppedAnimation<Color>(Colors.cyanAccent),
                      minHeight: 3,
                    ),
                    const SizedBox(height: 2),
                    Row(
                      mainAxisAlignment: MainAxisAlignment.spaceBetween,
                      children: [
                        Text(
                          isThisPlaying
                              ? _formatDuration(player.position)
                              : '0:00',
                          style: const TextStyle(
                            fontSize: 10,
                            color: Colors.white60,
                            fontFamily: 'monospace',
                          ),
                        ),
                        Text(
                          _formatDuration(Duration(milliseconds: _getRealDurationMs())),
                          style: const TextStyle(
                            fontSize: 10,
                            color: Colors.white60,
                            fontFamily: 'monospace',
                          ),
                        ),
                      ],
                    ),
                  ],
                ),
              ),
              const SizedBox(width: 8),
              IconButton(
                iconSize: 18,
                padding: EdgeInsets.zero,
                constraints: const BoxConstraints(),
                tooltip: "Export Audio (.wav)",
                icon: const Icon(Icons.download_rounded, color: Colors.cyanAccent),
                onPressed: () => _exportAudio(context),
              ),
            ],
          ),
        );
      },
    );
  }

  Future<void> _exportAudio(BuildContext context) async {
    final file = File(audioFilePath);
    if (!file.existsSync()) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('Audio file not found on disk.')),
      );
      return;
    }
    final suggestedName = p.basename(audioFilePath);
    try {
      final bytes = await file.readAsBytes();
      final resultUri = await FilePicker.saveFile(
        dialogTitle: 'Export Audio (.wav)',
        fileName: suggestedName.isNotEmpty ? suggestedName : 'transmission.wav',
        bytes: bytes,
        type: FileType.custom,
        allowedExtensions: ['wav'],
      );
      if (resultUri != null && context.mounted) {
        final displayName = p.basename(resultUri.path);
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Audio exported to $displayName')),
        );
      }
    } catch (e) {
      if (context.mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Export failed: $e')),
        );
      }
    }
  }

  int _getRealDurationMs() {
    try {
      final file = File(audioFilePath);
      if (file.existsSync()) {
        final bytes = file.lengthSync();
        final dataBytes = bytes > 44 ? bytes - 44 : bytes;
        return (dataBytes / 96000.0 * 1000).toInt();
      }
    } catch (_) {}
    return durationMs;
  }

  String _formatDuration(Duration d) {
    final mins = d.inMinutes;
    final secs = d.inSeconds % 60;
    return '$mins:${secs.toString().padLeft(2, '0')}';
  }
}
