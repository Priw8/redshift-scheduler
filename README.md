## redshift-scheduler

Runs redshift (a program that changes color temperature of your screens and other things) at specified times.

### Why?

While redshift does provide means to run automatically, I found it unnecessairly convoluted - you set your location on earth, when is it considered night or day, temperatures for them, etc... Personally, I just want to set predefined hours so I made this program (and I also wanted to make something useful with GTK which I've never used before).

### Building

First you need the dependencies:

- `gtkmm-4.0` (version `4.6.0` or later)
- `redshift` (at runtime)

You'll also need [meson](https://mesonbuild.com/).

```sh
# Clone the repo
git clone https://github.com/Priw8/redshift-scheduler.git
cd redshift-scheduler

# Configure build directory
meson setup builddir

# Enter build dir and compile
cd builddir
ninja

# Run
./redshift-scheduler
```

### TODO

- run with window closed (needs tray icon which gtk4 doesn't support natively)
- add support for setting screen brightness and gamma

