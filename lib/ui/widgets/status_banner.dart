import 'package:flutter/material.dart';
import '../../core/modem_coordinator.dart';

class StatusBanner extends StatelessWidget {
  const StatusBanner({super.key});

  @override
  Widget build(BuildContext context) {
    final coordinator = ModemCoordinator.instance;

    return ValueListenableBuilder<bool>(
      valueListenable: coordinator.isSentryLockedNotifier,
      builder: (context, isLocked, _) {
        return ValueListenableBuilder<String?>(
          valueListenable: coordinator.activeProtocolNotifier,
          builder: (context, activeProto, _) {
            return Container(
              padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 5),
              color: isLocked
                  ? Colors.green.withOpacity(0.18)
                  : const Color(0xFF161B22),
              child: Row(
                children: [
                  // Sentry status indicator with pulse dot
                  Container(
                    width: 8,
                    height: 8,
                    decoration: BoxDecoration(
                      shape: BoxShape.circle,
                      color: isLocked ? Colors.greenAccent : Colors.cyanAccent,
                      boxShadow: isLocked
                          ? [
                              BoxShadow(
                                color: Colors.greenAccent.withOpacity(0.8),
                                blurRadius: 6,
                                spreadRadius: 2,
                              ),
                            ]
                          : null,
                    ),
                  ),
                  const SizedBox(width: 6),
                  Expanded(
                    child: Text(
                      isLocked
                          ? "LOCKED: ${activeProto ?? 'DEMODULATING'}"
                          : "SENTRY: PARALLEL MONITOR",
                      overflow: TextOverflow.ellipsis,
                      maxLines: 1,
                      style: TextStyle(
                        fontSize: 10.5,
                        fontWeight: FontWeight.bold,
                        letterSpacing: 0.3,
                        color: isLocked ? Colors.greenAccent : Colors.cyanAccent,
                        fontFamily: 'monospace',
                      ),
                    ),
                  ),
                  const SizedBox(width: 6),
                  // Callsign Badge
                  ValueListenableBuilder(
                    valueListenable: coordinator.settingsNotifier,
                    builder: (context, settings, _) {
                      final call = settings.callsign;
                      return Container(
                        padding: const EdgeInsets.symmetric(horizontal: 5, vertical: 2),
                        decoration: BoxDecoration(
                          color: call != null ? Colors.blueGrey.withOpacity(0.3) : Colors.amber.withOpacity(0.2),
                          borderRadius: BorderRadius.circular(4),
                          border: Border.all(
                            color: call != null ? Colors.blueGrey : Colors.amber,
                            width: 0.8,
                          ),
                        ),
                        child: Text(
                          call != null ? "CALL: $call" : "NO CALLSIGN",
                          style: TextStyle(
                            fontSize: 9.5,
                            fontFamily: 'monospace',
                            fontWeight: FontWeight.bold,
                            color: call != null ? Colors.white70 : Colors.amber,
                          ),
                        ),
                      );
                    },
                  ),
                  const SizedBox(width: 6),
                  // Loopback badge
                  ValueListenableBuilder(
                    valueListenable: coordinator.settingsNotifier,
                    builder: (context, settings, _) {
                      return Container(
                        padding: const EdgeInsets.symmetric(horizontal: 5, vertical: 2),
                        decoration: BoxDecoration(
                          color: settings.isLoopbackEnabled
                              ? Colors.purple.withOpacity(0.3)
                              : Colors.white10,
                          borderRadius: BorderRadius.circular(4),
                          border: Border.all(
                            color: settings.isLoopbackEnabled
                                ? Colors.purpleAccent
                                : Colors.white24,
                            width: 0.8,
                          ),
                        ),
                        child: Text(
                          settings.isLoopbackEnabled ? "LOOPBACK" : "LIVE MIC",
                          style: TextStyle(
                            fontSize: 9.5,
                            fontFamily: 'monospace',
                            color: settings.isLoopbackEnabled
                                ? Colors.purpleAccent
                                : Colors.white60,
                          ),
                        ),
                      );
                    },
                  ),
                ],
              ),
            );
          },
        );
      },
    );
  }
}
