set(Sources
	src/log.cpp
)

set(Headers
	log.hpp
	report.hpp
)

set(Inlines
)

add_library (solid_audit_log
	${Sources} ${Headers} ${Inlines}
)

add_subdirectory (log)
