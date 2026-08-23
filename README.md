update this in the future, but ensure that hostapd and dnsmasq (all dependencies the backend uses) are installed on your system

22/08/2026 update
Work on frontend mainly now:
    - fix up connection screen (display SSID and Primary DNS to insert) (DONE)
    - display error message on main menu upon connection issue. display until menu changes. (DONE)
    - add settings menu to configure country code. ensure mandatory to configure before starting any connection.
    - add instruction menu ("how to set up a connection, both on IVnet and on the DS")
    - add refresh button to nic menu (DONE)
    - add timer to connection menu (DONE but timer is slow)
last thing to do is clean up backend and frontend and add comments.
