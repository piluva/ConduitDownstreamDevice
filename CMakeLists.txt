#Copyright (c) Microsoft. All rights reserved.
#Licensed under the MIT license. See LICENSE file in the project root for full license information.

if(NOT ${use_mqtt} AND NOT ${use_amqp} AND NOT ${use_http})
    message(FATAL_ERROR "conduit_downstream_device being generated without protocol support")
endif()

compileAsC99()

set(iotedge_downstream_c_files
    conduit_downstream_device.c
)

IF(WIN32)
    #windows needs this define
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
ENDIF(WIN32)

include_directories(.)

add_executable(conduit_downstream_device ${iotedge_downstream_c_files})
if(${build_as_dynamic})
    target_link_libraries(conduit_downstream_device iothub_client_dll)
else()
    target_link_libraries(conduit_downstream_device iothub_client)
    if(${use_mqtt})
        target_link_libraries(conduit_downstream_device iothub_client_mqtt_transport iothub_client_mqtt_ws_transport)
        linkMqttLibrary(conduit_downstream_device)
        add_definitions(-DUSE_MQTT)
    endif()
endif()
