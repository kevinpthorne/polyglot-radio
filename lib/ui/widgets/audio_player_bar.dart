import 'package:flutter/material.dart';
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
                          _formatDuration(Duration(milliseconds: durationMs)),
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
            ],
          ),
        );
      },
    );
  }

  String _formatDuration(Duration d) {
    final mins = d.inMinutes;
    final secs = d.inSeconds % 60;
    return '$mins:${secs.toString().padLeft(2, '0')}';
  }
}
