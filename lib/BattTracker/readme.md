Tracks battery status, accounting for charging, for noise

There are three high level battery states of charge:

 * high: 3.95V and above
 * low: 3.4V and below
 * medium: anything in between

The battery can also be charging (USB plugged in)

There are 6 icons to display to the user. The enum used should have 7 items (one for unknown), but one bit is used to indicate charging or not

There is an init function, a poll function, and a status getting function

When not charging, the tracked voltage can only go down, if a previously detected voltage is lower than the most recent poll, ignore the most recent poll.

During charging (USB plugged in), implement a hysterisis of about 0.1V for state transitions.

Transitions between charging and not-charging have no rules.

Internally have HAL intermediate functions for reading battery voltage as a float and a boolean for USB available yes or no. In this current implementation, use as much of the M5Unified library as you can.

Polling rate is fixed at once per second.

There is only one battery, only one instance of this is needed, use namespace instead of class
