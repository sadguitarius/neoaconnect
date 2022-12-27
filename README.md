## neoaconnect - ALSA sequencer connection manager
Copyright (C) 2022 Ben Goldwasser
based on aconnect by Takashi Iwai

TODO: fix license

Usage:
 * Connection/disconnection between two ports
   neoaconnect [-options] sender receiver
     sender, receiver = client:port pair
     -d,--disconnect     disconnect
     -e,--exclusive      exclusive connection
     -r,--real #         convert real-time-stamp on queue
     -t,--tick #         convert tick-time-stamp on queue
 * List connected ports (no subscription action)
   neoaconnect -i|-o [-options]
     -i,--input          list input (readable ports)
     -o,--output         list output (writable ports)
     -l,--list           list current connections of each port
     -p,--ports          list only port names 
                         (for shell completion scripts)
 * Remove all exported connections
     -x,--removeall
 * Serialization of connections in TOML format
    -s,--serialize      read current connections to terminal
    -S FILENAME,
--deserialize    repopulate connections from TOML file;

Most functionality is similar or identical to aconnect.

If a port name is unique (i.e. not "Port 1" but the actual name of the device), it can be connected using a shortcut naming convention  ":PORTNAME" instead of typing the full client:port names. This is amenable to creating shell completion scripts. (example forthcoming)

neoaconnect can save the state of all current connections using TOML. This must currently be piped to a file manually but can then be restored by passing the saved file as a parameter to the -S option.

## install
TODO: needs proper install procedure

For now, use cmake and copy the built binary to the preferred location.
