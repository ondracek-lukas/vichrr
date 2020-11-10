**Virtual Choir Rehearsal Room**
is a terminal application for low-latency multiparty audio calls optimized for singing.
A client application is available for Windows, Linux, and MacOS.
Linux server with good connectivity is needed as a mixing point.

Copyright © 2020  Lukáš Ondráček <<ondracek.lukas@gmail.com>>.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License version 3
as published by the Free Software Foundation.


Features
--------

* Uncompressed audio transfer, 16-bit, 48 kHz.
	* Mono recording, stereo playback.
* Low latency.
	* Sending sound by 2.67ms blocks over UDP.
	* Variable buffer size based on current connection quality.
	* Typical latency on distance of hundreds of km was 75 ms incl. 25 ms of sound system delay.
* Surround sound.
	* Each voice comes from different direction.
	* Achieved mainly by phase shift between left and right channel.
	* Order of voices can be changed.
* Semi-automatic microphone volume calibration before connection.
* Precise latency measurement, incl. sound system latency.
* Metronome synchronized according to current latency of each client.
* Server-side recording.
* Free and open-source (under GNU GPLv3).


Requirements
------------

**Client-side:**

* Linux, Windows, or MacOS.
* PortAudio library (included in Windows release).
* Network connectivity:
	* 1 Mbps upload, 1.7 Mbps download.
	* Cable connection may significantly lower latency in comparison with WiFi.
* Headphones with microphone.
	* There is no echo cancellation, loudspeaker cannot be used.

**Server-side:**

* Network connectivity:
	* Bandwidth per client: 1.7 Mbps upload, 1 Mbps download.
  * Cable with low ping to backbone recommended.
* Disk space:
	* 12 MB per one minute of recording.


Installation
------------

**Windows client:**

* Download latest version from _[releases]_ section or see compilation process below.
* Open _Control Panel_ > _Sound_ > _Manage audio devices_ and for relevant _Playback_/_Recording_ devices open _Properties_:
	* On _Enhancements_ tab check _Disable all enhancements_.
	* On _Advanced_ tab set _Default Format_ to _2 channel, 16 bit, 48000 Hz_,
	* ... check _Allow applications to take exclusive control of this device_,
	* ... check _Give exclusive mode applications priority_.
	* Keep _Levels_ tab of a microphone open for adjusting its volume after application is launched.
* Run the application.

Allowing exclusive control of your sound devices
means granting permission to bypass system audio mixer.
It significantly lowers latency of your sound system (to ~30 ms).
You will not hear sounds from other applications until this application is closed.

You can further reduce sound system latency by installing ASIO drivers.

Keep your computer AC-powered while using this application;
otherwise it may not work properly due to battery saving features.


**Linux client:**

* Download latest version from _[releases]_ section or see compilation process below.
* Install PortAudio Library (i.e. libportaudio, portaudio, or similar).
* Run the application.

Latency of PulseAudio sound system is ~20 ms,
you can further reduce is by bypassing it (using ALSA),
or using JACK sound system.


**MacOS client:**

Currently no precompiled binaries are available,
see compilation process below or use the following simplified inctructions:

* Download install-mac.sh script from _[releases]_ section to your desktop.
* Open terminal and write there the following two lines:
    ```
    chmod +x Desktop/install-mac.sh
    Desktop/install-mac.sh
    ```
* Enter password for installing applications when prompted.
* A link to the application should have been created on desktop.

The script installs:
* Xcode Command Line Tools including gcc, make and git,
* HomeBrew for installing PortAudio,
* PortAudio library,
* the application.

For later updates write the following line to terminal:

   virtual-choir-rehearsal-room/update.sh


**Linux server:**

* See compilation process below.


Client usage
------------

The application is controlled with keyboard only
and you will always see which key may be used for which action;
so read carefully whether you should press _space_, _y_/_n_, etc.

After launching the application four steps should be done before connecting,
which are described in greater detail in the application window:

1. Selecting sound interface and devices:
	* On Windows, WASAPI (i.e. exclusive mode) is selected by default.
	* On Linux, PulseAudio is usually the default option.
2. Measuring delay of sound system:
	* Several loud beeps are played to your headphones and recorded by you microphone.
3. Setting microphone and headphones' volume:
	* You should sing loudly in this step to let the application adjust in-application microphone volume.
	* You will also see whether the system microphone volume is not too high and should lower it down in system settings.
4. Setting your server address and your name.

After connecting you will see list of participants ordered from left ear to right one,
with following information:

    .name      AA+BB ms [########++++++------------------------]  -CC dB ( -DD dB)

Here:
* Dot before name marks your line;
* `AA` is the round-trip delay of sound system;
* `BB` is all the other round-trip delay (in network, server, etc.);
* `#` sign marks average sound intensity level;
* `+` sign marks peak intensity level;
* `-CC` is average intensity level in dB (with zero being maximum what can be transferred);
* `-DD` is peak intensity level.

Below the list, other settings are printed
as well as the list of possible actions along with their assigned keys.

Each client can:
* move itself up/down in the list;
* toggle server-side recording;
* control shared metronome.


Server usage
------------

Just compile and run, there are no further settings.

Recordings are being saved under current working directory,
so it may be good idea to change it in advance.
You may also want to log standard output of the application
since it may contain useful information.


Compilation
-----------

Compilation was tested on Linux only, incl. cross-compiling for Windows.

Dependencies:
* Git (for getting source code)
* GNU Make
* GCC
* PortAudio library (only for client)
* Mingw-w64 (only for cross-compiling)

Getting sources:

    git clone 'https://github.com/ondracek-lukas/virtual-choir-rehearsal-room.git'
    cd virtual-choir-rehearsal-room

Compiling native client:

    make client

Compiling native server:

    make server

Cross-compiling client for Windows:

    make client32.exe client64.exe
    # attach relevant dll libraries from your mingw installation

Updating to newer version:

    make clean
    git pull
    make ...


See also
--------

* [Virtual Choir Rehearsal Room][1] on github.

[1]: https://github.com/ondracek-lukas/virtual-choir-rehearsal-room
[releases]: https://github.com/ondracek-lukas/virtual-choir-rehearsal-room/releases
