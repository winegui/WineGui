# Wine GUI
Finally, an user-interface friendly [WINE](https://www.winehq.org/) Manager.

## Development

### Requirements

Dependencies should be met before build:

* cmake
* GTK3.0+
* pkg-config
* Doxygen

### Build

Run: `./build.sh`

### Run

Execute:
`./build/bin/winegui`

Or go to the `build` directory and execute:

```
ninja run
```

### Rebuild

Cmake is only needed once, after that you can often use:

`ninja`

Clean the build via: `ninja clean`

*Hint:* Run `ninja help` for all available targets.

## Coding standard

We follow the [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html).
