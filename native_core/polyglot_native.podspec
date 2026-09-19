Pod::Spec.new do |s|
  s.name             = 'polyglot_native'
  s.version          = '0.1.0'
  s.summary          = 'Polyglot Radio Acoustic SDR Native C++ Core'
  s.homepage         = 'https://github.com/polyglot-radio'
  s.license          = { :type => 'MIT' }
  s.author           = { 'Polyglot Radio Team' => 'dev@polyglot.radio' }
  s.source           = { :path => '.' }

  s.ios.deployment_target = '14.0'

  s.source_files = [
    'src/**/*.{c,cpp}',
    'include/**/*.h',
    'external/**/*.h',
  ]

  s.public_header_files = 'include/native_bridge.h'

  s.pod_target_xcconfig = {
    'CLANG_CXX_LANGUAGE_STANDARD' => 'c++20',
    'CLANG_CXX_LIBRARY' => 'libc++',
    'HEADER_SEARCH_PATHS' => '"${PODS_TARGET_SRCROOT}/include" "${PODS_TARGET_SRCROOT}/external"',
    'OTHER_CPLUSPLUSFLAGS' => '-std=c++20 -x objective-c++',
    'DEFINES_MODULE' => 'YES'
  }

  s.frameworks = 'CoreAudio', 'AudioToolbox', 'AVFoundation'
  s.libraries = 'c++'
end
