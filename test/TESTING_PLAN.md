Unit tests
	- Easy
		- pawned_pw (each method / function, normal)
		- `flat_file`
			- `flat_file::db` (inject `std::string_stream?` or file?))
			- `stream_writer` (inject `std::string_stream`?)
		- arrcmp (alreday done?)
		- md4
		- ntlm
	- Hard
		- dnl/queuemgt|requests
		
Integration tests
	- eg write `test_apps` using same api as `app/hibp_*` and
		injecting "mock writers", verifying the content they capture
			

DONE
====

System test (Golden master)
	- maybe use shunit2
	- will require 
	- each program in app
	- will require source of files downloadable from simple, local
      (python?) webserver

Integration tests
	- done for diff (although not for all hash types)
	- done for search
