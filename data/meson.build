data_dir = join_paths(get_option('prefix'), get_option('datadir'))
gnome = import('gnome')

sources += gnome.compile_resources('megatiflo-resources', 
				   'com.github.andrejgricenko.megatiflo.gresource.xml')				 

install_data('com.github.andrejgricenko.megatiflo.desktop',
	     install_dir: join_paths(data_dir, 'applications'))
install_data('com.github.andrejgricenko.megatiflo.png',
	     install_dir: join_paths(data_dir, 'icons', 'hicolor', '256x256', 'apps'))