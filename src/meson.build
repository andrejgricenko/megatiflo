sources += ['main.c', 
	   'megatiflo-window.c']
				   
dependencies = [dependency('gtk+-3.0'), 
		dependency('libsoup-2.4')]

link_args = []
if build_machine.system() == 'windows'
	link_args += ['-mwindows']
endif

executable('megatiflo', sources,
	   dependencies: dependencies,
	   install: true,
	   link_args: link_args)