# LibAVANE-Android

LibAVANE-Android is an Android Studio module written in java and c++ to elegantly use FFmpeg in your projects.

```java
libAVANE = LibAVANE.getInstance();
libAVANE.setLogLevel(LogLevel.INFO);

Log.i("version",libAVANE.getVersion());
Log.i("build config",libAVANE.getBuildConfiguration());

libAVANE.eventDispatcher.addEventListener(Event.ON_ENCODE_PROGRESS, new IEventHandler() {
	@Override
	public void callback(Event event) {
		final Progress progress = (Progress) event.getParams();
		final Double percent = (progress.secs + (progress.us/100))/duration;
	}
});


InputOptions inputOptions = new InputOptions();
inputOptions.uri = "http://download.blender.org/durian/trailer/sintel_trailer-1080p.mp4";
inputOptions.duration = 10.0;
InputStream.clear();
InputStream.addInput(inputOptions);

//video
OutputVideoStream videoStream = new OutputVideoStream();
videoStream.codec = "libx264";
videoStream.crf = 22;

X264Options x264Options = new X264Options();
x264Options.preset = X264Preset.ULTRA_FAST;
x264Options.profile = X264Profile.MAIN;
x264Options.level = "4.1";
videoStream.encoderOptions = x264Options;

OutputOptions.addVideoStream(videoStream);

//audio
OutputAudioStream audioStream = new OutputAudioStream();
audioStream.codec = "aac";
OutputOptions.addAudioStream(audioStream);

OutputOptions.uri = appDirectory + "/files/avane-encode-classic.mp4";

libAVANE.encode();

```

The FFmpeg command can be built using an OOP approach as above or constructed in a CLI fashion like below

```java
String params = "-i " +
"http://download.blender.org/durian/trailer/sintel_trailer-1080p.mp4 " +
"-c:v libx264 -crf 22 -c:a copy -preset ultrafast -y "
+ appDirectory + "/files/avane-encode-classic.mp4";

libAVANE.encode(params);
```
Includes ability to probe file for information

```java
libAVANE.triggerProbeInfo("http://download.blender.org/durian/trailer/sintel_trailer-1080p.mp4");

libAVANE.eventDispatcher.addEventListener(Event.ON_PROBE_INFO_AVAILABLE, new IEventHandler() {
    @Override
    public void callback(Event event) {
        Probe probe = libAVANE.getProbeInfo();
        duration = probe.format.duration;
    }
});

```

Please see an example Android project at [https://github.com/tuarua/LibAVANE-Android-Example]

Supported Architectures
----
* arm
* armv7
* x86
