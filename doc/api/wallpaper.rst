The wallpaper
=============

The library's thread picks and decodes the next wallpaper
(plugins/wallpaper.c), awl takes the decoded image on the main thread and
shows it, fading from the previous one (background.c). The modes the
``wallpaper`` and ``wallpapermode`` actions take are `WallpaperMode`, the
settings `WallpaperConfig`; both are in :doc:`awl.h <awl>`.

background.h
------------

.. c:autodoc:: background.h

background.c
^^^^^^^^^^^^

.. c:autodoc:: background.c

plugins/wallpaper.h
-------------------

.. c:autodoc:: plugins/wallpaper.h

plugins/wallpaper.c
^^^^^^^^^^^^^^^^^^^

.. c:autodoc:: plugins/wallpaper.c
