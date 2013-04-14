My announcement
============
I have reviewed the putty6.1's code and merge to the master branch.
It has been changed a lot on the config structure, on which I did lots of extentions.
So it is not easy to merge.
Since there are not too many new features(putty-nd's code bases on the commit early this year) and big bugs fixing, I'd like not to merge it to the putty-nd's code at the moment.


My comments
============
Any comment is appreciated.
Email: noodle1983@126.com 
PalPay: noodle1983@126.com

RELEASE NOTE
============
--------------------------------
putty6.0_nd1.15
--------------------------------
1. Bugs
    1. The window is maximized with wrong size on my second screen on Windows 7.

2. Other changes
    1. The setting dialog and the main window are shown in the same screen.
    2. Type down arrow key to sellect the session in the setting dialog.

3. Known issue
    1. crashes happens when maximized and changing the font. The root cause is unknown yet.

--------------------------------
putty6.0_nd1.14
--------------------------------
1. Bugs
    1. The window is maximized with wrong size on my second screen on Windows 7.
    2. Crash bug when telnet/raw tab exits from time to time.

2. Other changes
    1. expand the width of the session treeview.

--------------------------------
putty6.0_nd1.13
--------------------------------
1. Bugs
    1. config dialog can be opened twice and then crash.

2. Other changes
    1. backup/restore(import/export) the sessions.
    2. make a config item valid: Session->Close window/tab on exit.

--------------------------------
putty6.0_nd1.12
--------------------------------
1. Bugs
    1. crash bug: ldisc's frontend should be set as tabitem. it happens when connecting to a raw telnet server.
    2. one Makefile issue while cross-compiling putty-nd on linux for Windows in Recipe file.

2. Other changes
    1. keyboard shortcuts: Ctrl+[Shift]+Tab to shift tab; Ctrl+[Shift]+` to move tab; Ctrl+Shift+n to new a session.

--------------------------------
putty6.0_nd1.11
--------------------------------
1. Other changes
    1. make it possible to change the tab title dynamically. How to do:
       a. un-tick setting Terminal->Featrues->Disable remote-controlled tab title...
       b. run cmd:PROMPT_COMMAND='echo -ne "\033]0;the_tab_title\007"'. The tab title can contain the system env, for example:
           PROMPT_COMMAND='echo -ne "\033]0;${USER}@${HOST}: ${PWD}\007"'
    2. a very good idea from Lokesh: One click logging feature to capture terminal prints. This modified code will have an option for capturing the onscreen prints to a log file. Even though this is already a feature of Putty in "logging" section but there is no straight forward mechanism to Start & Stop whenever required. So two options have been provided:
       a. A menu entry "Start Logging" when we right click anywhere inside terminal (for this to work "Selection" behaviour has to be "windows").
       b. A toolbar button ">" for Start & Stop Logging.
    3. more options for log file's name(&S for session name, and &P for the desktop path).


--------------------------------
putty6.0_nd1.10
--------------------------------
1. Other changes
    1. keyboard shortcuts for tabs: Alt+Num: switch to tab; Alt+`/Right: iterate the tab forward; Alt+Left; iterate the tab backword; Ctrl+Shift+t: duplicate the tab.

--------------------------------
putty6.0_nd1.9
--------------------------------
1. Other changes
    1. when duplicating a tab, the new tab will be placed next to the current tab.

--------------------------------
putty6.0_nd1.8
--------------------------------
1. Bug fixed
    1. when using Vim in a session for a long time and then creating another session, it may crash. It happened in my work env from time to time. Root cause is unknown. I changed something by guessing. And the issue didn't happen for 2 weeks.

2. Other changes
    1. When openning a session via Default Setting, the session will be saved with name "tmp/host:port". And the title will show as "host:port" instead of "Default Setting".

--------------------------------
putty6.0_nd1.7
--------------------------------
1. Bug fixed
    1. after reconfiguration, current selected session in tab will be saved to the lastest open session.

2. Other changes
    1. draw the main window's edge

--------------------------------
putty6.0_nd1.6
--------------------------------
1. Bug fixed
    1. crash at exit when one of the session's hostname can't be resolved. the 
crash only happens when the process exits.

2. Other changes
    1. when enter key is typed on a closed session, it restarts the session.
    2. tab title shows gray if session is closed.

--------------------------------
putty6.0_nd1.5
--------------------------------
1. New Features
    1. searchbar

2. Other changes
    1. Add tooltips for the toolbar buttons.
    2. In the session manager, save the expanding status of the session group.

--------------------------------
putty6.0_nd1.4
--------------------------------
1. Bugs fixed
    1.1. do not reset win_title after re-configured. 
    1.2. resize the length of the tabbar, to avoid covered by the system botton.
	
--------------------------------
putty6.0_nd1.3
--------------------------------
1. Bugs fixed
    1.1. assert failed when reconfig. 
    1.2. fullscreen is not supported yet, fix a scrollbar issue when zoomed.
	
--------------------------------
putty6.0_nd1.2
--------------------------------
1. Bugs fixed
    1.1. LICENSE. 
    1.2. resize term when swith tab.

--------------------------------
putty6.0_nd1.1
--------------------------------
1. Bugs fixed
    1.1. crash when paste by shift+insert. 
    1.2. not to show special menu when right click on the page.
    1.3. fix bug when maximize on multi-monitor.
    1.4. bind logevent to a tab; if not appliable, dump to debug log.

--------------------------------
putty6.0_nd1.0
--------------------------------
1. New Features
    1. tabbar

2. Other changes
    2.1 merge from putty6.0_nd0.3

3. Bugs fixed
    3.1. window's title is not set. 
    3.2. It only has a left-top archor when sizing.
    3.3. no right click memu

--------------------------------
putty6.0_nd0.3
-------------------------------- 
1. Bugs Fix
    1.1 rename the last session and do some changes and open, the cfg is saved as previous session.

--------------------------------
putty6.0_nd1.0-pre
--------------------------------
1. New Features
    1. tabbar

2. Other changes
    2.1 merge from putty6.0_nd0.2

3. Bugs remains
    3.1. window's title is not set. 
    3.2. It only has a left-top archor when sizing.
    3.3. no right click memu

--------------------------------
putty6.0_nd0.2
-------------------------------- 
1. Bugs Fix
    2.1 automate logon does not work sometimes.
    

--------------------------------
putty6.0_nd0.1
--------------------------------
1. New Features
    1.1 Session Management.
    1.2 Automate Logon.
    1.3 Scroll lines can be configured when the middle button of the mouse is scrolled. The default lines' number is 3.