# Canonical physical ownership shared by preparation, export and backend audits.
namespace eval amf_full {
    variable fabric {LUT1 LUT2 LUT3 LUT4 LUT5 LUT6 LUT6_2 FDRE FDSE FDCE FDPE LDCE AND2B1L CARRY8 DSP48E2 MUXF7 MUXF8 SRL16E SRLC32E RAM32M16 RAM64M RAM64X1D RAM32M RAM32X1D RAM32X1S RAM64X1S RAM64M8 RAM256X1D RAM256X1S FIFO36E2 FIFO18E2 RAMB18E2 RAMB36E2 URAM288 URAM288_BASE GND VCC}
    variable interfaces {IBUF IBUFDS IBUF_ANALOG IBUFDS_GTE3 IBUFDS_GTE4 OBUF OBUFT IOBUF IOBUFE3 IOBUFDS OBUFDS_DUAL_BUF BUFGCE BUFGCE_DIV BUFGCTRL BUFG_GT BUFG_GT_SYNC MMCME3_ADV MMCME4_ADV PLLE3_ADV PLLE4_ADV GTHE3_CHANNEL GTHE3_COMMON GTYE4_CHANNEL GTYE4_COMMON PCIE_3_1 PCIE40E4 RXTX_BITSLICE BITSLICE_CONTROL TX_BITSLICE_TRI OSERDESE3 RIU_OR HPIO_VREF BSCANE2 SYSMONE1}
    variable composite {IBUF IBUFDS IBUF_ANALOG OBUF OBUFT IOBUF IOBUFE3 IOBUFDS OBUFDS_DUAL_BUF}
    proc properties {key objects} {
        set result {}
        for {set offset 0} {$offset < [llength $objects]} {incr offset 512} {
            lappend result {*}[get_property $key [lrange $objects $offset [expr {$offset+511}]]]
        }
        return $result
    }
    proc canonical_cells {} {
        set all [get_cells -hier -filter {IS_PRIMITIVE}]
        set names [properties NAME $all]
        set seen [dict create]
        foreach n $names {dict set seen $n 1}
        set result {}
        foreach c $all n $names {
            set parent $n;set internal 0
            while {[set slash [string last / $parent]]>=0} {
                set parent [string range $parent 0 [expr {$slash-1}]]
                if {[dict exists $seen $parent]} {set internal 1;break}
            }
            if {!$internal} {lappend result $c}
        }
        return $result
    }
    proc canonical_interfaces {} {
        variable interfaces
        set filters {}
        foreach kind $interfaces {lappend filters "REF_NAME == $kind"}
        set cells [get_cells -hier -filter "IS_PRIMITIVE && ([join $filters { || }])"]
        set names [properties NAME $cells]
        set result {}
        foreach c $cells name $names {
            set parent $name;set internal 0
            while {[set slash [string last / $parent]] >= 0} {
                set parent [string range $parent 0 [expr {$slash-1}]]
                set ancestor [get_cells -quiet $parent]
                if {[llength $ancestor] && [get_property IS_PRIMITIVE $ancestor]} {set internal 1;break}
            }
            if {!$internal} {lappend result $c}
        }
        puts "CANONICAL_INTERFACES [llength $result]";flush stdout
        return $result
    }
    proc interface {kind} {
        variable fabric;variable interfaces
        if {$kind in $fabric} {return 0}
        if {$kind in $interfaces} {return 1}
        error "Unsupported canonical primitive: $kind"
    }
    proc target_mode {kind} {
        variable composite
        if {$kind in $composite || $kind eq "BUFGCE"} {return site}
        return bel
    }
    proc bool_prop {object key} {
        set v [get_property $key $object]
        return [expr {$v in {1 true TRUE}}]
    }
    variable driverCache [dict create]
    proc driver {pin} {
        variable driverCache
        set top [get_nets -quiet -top_net_of_hierarchical_group -of_objects $pin]
        if {![llength $top]} {return {}}
        if {[dict exists $driverCache $top]} {return [dict get $driverCache $top]}
        if {$top eq "<const0>"} {return @CONST/0}
        if {$top eq "<const1>"} {return @CONST/1}
        set nets [get_nets -quiet -segments $top]
        set drivers [lsort -unique [get_pins -quiet -leaf -of_objects $nets -filter {DIRECTION == OUT}]]
        if {![llength $drivers]} {set drivers [get_ports -quiet -of_objects $nets -filter {DIRECTION == IN}]}
        set result [join $drivers ,]
        dict set driverCache $top $result
        return $result
    }
}
