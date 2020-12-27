# mousekeys.c

A simple program that generates keyboard strokes on mouse button presses. As-is
it generates `C-PgUp` when the left thumb button (back button) is pressed and
`C-PgDwn` when the right thumb button (forward button) is pressed.  You can edit
the variables `action_right`, `action_left` and `output_codes` to customize
this.

Works with Wayland, X, whatever else.

To install:

    make && sudo make install


It will automatically run through udev for each mouse that is plugged in
afterwards. To run it immediately without unplugging and replugging your mouse:

    printf "%s\n" /dev/input/by-path/*event-mouse | awk -F/ '{print "mousekeys@" substr($5, 0, length($5) - 12) ".service" }' | sudo xargs systemctl restart


Note that the original mouse events are not suppressed and still reach
applications. In Firefox, you can disable back/forward actions in about:config
by setting these options to `false`:

    mousebutton.4th.enabled
    mousebutton.5th.enabled
