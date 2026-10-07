The desktop panel
=================

The list of the files in ``desktop_dir`` (a config dictionary key,
``$HOME/Desktop`` by default) over the wallpaper. awl places the
panels and owns their buffers (desktop.c), the library scans the directory
and draws the list (desktop_panel.c).

desktop.h
---------

.. c:autodoc:: desktop.h

desktop.c
^^^^^^^^^

.. c:autodoc:: desktop.c

desktop_panel.c
---------------

.. c:autodoc:: desktop_panel.c

plugins/readdir.h
-----------------

.. c:autodoc:: plugins/readdir.h
