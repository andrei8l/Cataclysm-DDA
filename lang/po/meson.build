if get_option('localize')
    i18n = import('i18n', required: false)
    if i18n.found()
        i18n.gettext(meson.project_name())
    endif
endif
