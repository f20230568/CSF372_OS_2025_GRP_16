# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
line1
returncarriage
echo: exit(0)
EOF
pass;
