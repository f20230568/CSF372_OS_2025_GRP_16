# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
  leading trailing     both  
echo: exit(0)
EOF
pass;
