# Fork of speechd-up: Simple interface between SpeakUp and Speech Dispatcher
The `speechd-up` package is a discontinued program created by the Free Software Foundation. Its object is to hook the linux `speech-dispatcher` program, a daemon which takes text from a screen reader to dispatch speech to a variety of supported speech synthesizers, with the `speakup` kernel module, which provides screen review to Linux terminal sessions. This fork adds functionality to synchronize the speech-dispatcher settings set in the configuration by the user, like pitch and volume, with the controls built into the `speakup` module accessible via the `/sys/accessibility/speakup` filesystem interface. The practical effect is to ensure consistency in speech settings across reboots and the ability to make on-the-fly adjustments to the parameters using the native keyboard controls bult into the speakup driver.
## Build and Installation
See also the `INSTALL` file in this repository.
In addition to the `speech-dispatcher` and `speakup` modules, `speechd-up` requires the `libdotconf` package to be installed, and `texinfo` must be installed for the documentation.
To build and install the package, run:
```
./build.sh
./configure
make all # run make speechd-up instead to skip compiling documentation
make install
```
Reference the README and documentation for more specific information on how to use the `speechd-up` binary.