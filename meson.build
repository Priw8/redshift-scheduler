project('redshift-scheduler', 'cpp')

fs = import('fs')

gtkmm_dep = dependency('gtkmm-4.0', version: '>= 4.6.0')

executable('redshift-scheduler', 'src/main.cpp', dependencies : gtkmm_dep)
fs.copyfile('data/redshift-scheduler.ui', 'redshift-scheduler.ui')
fs.copyfile('data/redshift-scheduler.css', 'redshift-scheduler.css')
