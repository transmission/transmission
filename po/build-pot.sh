pushd ..
xgettext --default-domain=transmission-gtk \
         --from-code=UTF-8 \
         --keyword\=\_ \
         --keyword\=N\_ \
         --keyword\=U\_ \
         --keyword\=Q\_ \
         *\.[ch]
mv transmission-gtk.po po/transmission-gtk.pot
