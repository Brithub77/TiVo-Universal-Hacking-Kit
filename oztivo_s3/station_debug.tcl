#!/tvbin/tivosh
set ::db [dbopen [expr 300 * 1024]]

puts "digging through stations, this takes a while..."
proc mfs_dump_obj id {
  set ret {}
  RetryTransaction {
    foreach {fsid sub} [split $id /] break
    if {$sub!=""} {
      set obj [db $::db openidconstruction $fsid $sub]
    } else {
      set obj [db $::db openid $fsid]
    }
    foreach attr [dbobj $obj attrs] {
      set link 0
      if { [string range $attr 0 1]=="0x" } {
        set val "(attribute $attr not in schema)"
        set type "bad"
      } else {
        set type [dbobj $obj attrtype $attr]
        switch -exact $type {
          "subobject" -
          "object"  {
              set val [dbobj $obj gettarget $attr]
              set link 1
            }
          "file" {
              set val [dbobj $obj get $attr]
              if [catch {mfs streamsize $val}] { set link 1 } else {set link 2}
            }
          default {
              set val [dbobj $obj get $attr]
            }
        }
      }
      lappend ret $attr $link $type $val
    }
    set ret [list [dbobj $obj type] [dbobj $obj fsid] [dbobj $obj subobjid] [dbobj $obj construction] $ret]
  }
  return $ret
}

proc transport_id id {
      foreach {type id sub constr fields} [mfs_dump_obj $id] break
      foreach {attr islink atype val} $fields {
		#putlog "$attr:$val" 
		if {$attr == "DvbTransportStreamId"} {
			return $val;	
		}
      }
	
}


array set stations {}

proc all_stations {} {
	set trans ""
	set num ""
	set server_id ""
	global stations
	ForeachMfsFile id name type "/StationTms" "" {                    
           foreach {type id sub constr fields} [mfs_dump_obj $id] break
           foreach {attr islink atype val} $fields {
           	 #putlog "$attr:$val, $num"  
           	 if {$attr=="FccChannelNum"} {
           	 	#putlog "$attr:$val"
           	 	set num  $val
           	 	
           	 }
           	 if {$attr=="StationNetworkInfo"} {
           	 	 set trans [transport_id $val] 
           	 } 
           	 if {$attr=="ServerId"} {
           	 	 set server_id  $val 
           	 } 
           }	 
           set stations($trans,$num) $val
           #break
   } 	
}
all_stations

ForeachMfsFile id name type "/State" "" {  
	  # putlog "$name"
	if {$name=="LastScanChannelHolder"} {
		   #putlog "StationTMS:$name"
		   foreach {type id sub constr fields} [mfs_dump_obj $id] break
		   foreach {attr islink atype val} $fields {
		   	  if {$attr=="Channels"} { 
		   	  	  foreach x $val {
		   	  	  	 # putlog "$x" 
		   	  	  	 foreach {type id sub constr channelFields} [mfs_dump_obj $x] break
		   	  	  	 #putlog "$channelFields"
					 set num ""
					 set stationName ""
					 set transportStreamId ""		   	  	  	 
		   	  	  	 foreach {chanAttr islink chanType val} $channelFields {

		   	  	  	 	 switch -exact $chanAttr { 
		   	  	  	 	 	"MajorNumber" {
		   	  	  	 	 		set num $val
		   	  	  	 	 		#putlog $num
		   	  	  	 	 	}
		   	  	  	 	 	"StationName" {
		   	  	  	 	 		set stationName $val
		   	  	  	 	 	}
		   	  	  	 	 	"TransportStreamId" {
		   	  	  	 	 		set transportStreamId $val	
							}
							default { 
							#putlog $chanAttr
							}
					 
						 }

		   	  	  	 }
		   	  	  	 global stations
					 putlog "Channel:$num, Name:$stationName, TransportID:$transportStreamId "
					 if { [info exists stations($transportStreamId,$num)] } {
						 putlog $stations($transportStreamId,$num)
					 } else {
					 	 putlog "NO STATION"
					 }
					 #find_station $num $transportStreamId $stationName

				  }
		   	  	 
		   	  	  #putlog "$id $attr:$val, $islink, $atype"  
		   	  }
		   }	
	   
	   } 
              
   } 
   
   
dbclose ::db
