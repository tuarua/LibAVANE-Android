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

String[] params = {"-i","http://download.blender.org/durian/trailer/sintel_trailer-1080p.mp4", "-c:v","libx264","-c:a","copy","-preset","ultrafast","-y", appDirectory + "/files/avane-encode-classic.mp4"};
libAVANE.encode(params);

```

Please see an example Android project at [https://github.com/tuarua/LibAVANE-Android-Example]

Supported Architectures
----
* arm
* armv7
* x86