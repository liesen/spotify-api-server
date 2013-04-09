init:
	npm install

ffi: init
	mkdir -p lib
	node node_modules/ffi-generate/bin/ffi-generate.js -f /usr/local/include/libspotify/api.h -l libspotify -p sp_ |\
	sed -E 's/u(long)+/uint64/' > ffi/libspotify.js
