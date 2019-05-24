%INTERFACE <UDPdiag>
&command
  : Local set packet size %d * {
      if_UDPdiag.Turf("S:%d\n", $5);
    }
  : Local set packet rate %d * {
      if_UDPdiag.Turf("R:%d\n", $5);
    }
  : Local Quit * {
      if_UDPdiag.Turf("Q\n");
    }
  : Remote set packet size %d * {
      if_UDPdiag.Turf("XS:%d\n", $5);
    }
  : Remote set packet rate %d * {
      if_UDPdiag.Turf("XR:%d\n", $5);
    }
  : Remote Quit * {
      if_UDPdiag.Turf("XQ\n");
    }
  ;