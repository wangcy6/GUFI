/*
This file is part of GUFI, which is part of MarFS, which is released
under the BSD license.


Copyright (c) 2017, Los Alamos National Security (LANS), LLC
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors
may be used to endorse or promote products derived from this software without
specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


From Los Alamos National Security, LLC:
LA-CC-15-039

Copyright (c) 2017, Los Alamos National Security, LLC All rights reserved.
Copyright 2017. Los Alamos National Security, LLC. This software was produced
under U.S. Government contract DE-AC52-06NA25396 for Los Alamos National
Laboratory (LANL), which is operated by Los Alamos National Security, LLC for
the U.S. Department of Energy. The U.S. Government has rights to use,
reproduce, and distribute this software.  NEITHER THE GOVERNMENT NOR LOS
ALAMOS NATIONAL SECURITY, LLC MAKES ANY WARRANTY, EXPRESS OR IMPLIED, OR
ASSUMES ANY LIABILITY FOR THE USE OF THIS SOFTWARE.  If software is
modified to produce derivative works, such modified software should be
clearly marked, so as not to confuse it with the version available from
LANL.

THIS SOFTWARE IS PROVIDED BY LOS ALAMOS NATIONAL SECURITY, LLC AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL LOS ALAMOS NATIONAL SECURITY, LLC OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
OF SUCH DAMAGE.
*/



#include <gtest/gtest.h>

extern "C" {

#include "bf.h"
#include "utils.h"

}

TEST(parse_cmd_line, flags) {
    const char opts[] = "xpPNVsbarRDYZu:";

    const std::string exec = "exec";
    const std::string x= "-x";
    const std::string p= "-p";
    const std::string P= "-P";
    const std::string N= "-N";
    const std::string V= "-V";
    const std::string s= "-s";
    const std::string b= "-b";
    const std::string a= "-a";
    const std::string r= "-r";
    const std::string R= "-R";
    const std::string D= "-D";
    const std::string Y= "-Y";
    const std::string Z= "-Z";
    const std::string u= "-u";

    const char *argv[] = {
        exec.c_str(),
        x.c_str(),
        p.c_str(),
        P.c_str(),
        N.c_str(),
        V.c_str(),
        s.c_str(),
        b.c_str(),
        a.c_str(),
        r.c_str(),
        R.c_str(),
        D.c_str(),
        Y.c_str(),
        Z.c_str(),
        u.c_str(),
        nullptr,
    };

    int argc = sizeof(argv) / sizeof(argv[0]);

    struct input in;
    ASSERT_EQ(parse_cmd_line(argc, (char **) argv, opts, 0, "", &in), argc);
    EXPECT_EQ(in.xattrs.enabled, 1);
    EXPECT_EQ(in.printing,       1);
    EXPECT_EQ(in.printdir,       1);
    EXPECT_EQ(in.printheader,    1);
    EXPECT_EQ(in.printrows,      1);
    EXPECT_EQ(in.writetsum,      1);
    EXPECT_EQ(in.buildindex,     1);
    EXPECT_EQ(in.andor,          1);
    EXPECT_EQ(in.insertfl,       1);
    EXPECT_EQ(in.insertdir,      1);
    EXPECT_EQ(in.dontdescend,    1);
    EXPECT_EQ(in.suspectd,       1);
    EXPECT_EQ(in.suspectfl,      1);
    EXPECT_EQ(in.infile,         1);
}

TEST(parse_cmd_line, options) {
    const char opts[] = "n:g:d:t:i:I:T:S:E:F:W:A:c:v:w:y:z:";
    const std::string exec = "exec";
    const std::string n = "-n"; const std::string n_arg = "1";
    const std::string g = "-g"; const std::string g_arg = "1";
    const std::string d = "-d"; const std::string d_arg = "|";
    const std::string t = "-t"; const std::string t_arg = "t arg";
    const std::string i = "-i"; const std::string i_arg = "i arg";
    const std::string I = "-I"; const std::string I_arg = "I arg";
    const std::string T = "-T"; const std::string T_arg = "T arg";
    const std::string S = "-S"; const std::string S_arg = "S arg";
    const std::string E = "-E"; const std::string E_arg = "E arg";
    const std::string F = "-F"; const std::string F_arg = "F arg";
    const std::string W = "-W"; const std::string W_arg = "W arg";
    const std::string A = "-A"; const std::string A_arg = "1";
    const std::string c = "-c"; const std::string c_arg = "1";
    const std::string y = "-y"; const std::string y_arg = "1";
    const std::string z = "-z"; const std::string z_arg = "1";

    const char *argv[] = {
        exec.c_str(),
        n.c_str(), n_arg.c_str(),
        g.c_str(), g_arg.c_str(),
        d.c_str(), d_arg.c_str(),
        t.c_str(), t_arg.c_str(),
        i.c_str(), i_arg.c_str(),
        I.c_str(), I_arg.c_str(),
        T.c_str(), T_arg.c_str(),
        S.c_str(), S_arg.c_str(),
        E.c_str(), E_arg.c_str(),
        F.c_str(), F_arg.c_str(),
        W.c_str(), W_arg.c_str(),
        A.c_str(), A_arg.c_str(),
        c.c_str(), c_arg.c_str(),
        y.c_str(), y_arg.c_str(),
        z.c_str(), z_arg.c_str(),
        // no nullptr since getopt will try to dereference it
    };

    int argc = sizeof(argv) / sizeof(argv[0]);

    struct input in;
    ASSERT_EQ(parse_cmd_line(argc, (char **) argv, opts, 0, "", &in), argc);
    EXPECT_EQ(in.maxthreads,          1);
    EXPECT_EQ(in.stride,              1);
    EXPECT_EQ(in.outfile,             0);
    EXPECT_EQ(in.outdb,               0);
    EXPECT_EQ(in.delim[0],            '|');
    EXPECT_STREQ(in.nameto,           t_arg.c_str());
    EXPECT_STREQ(in.name,             i_arg.c_str());
    EXPECT_STREQ(in.sql.init,         I_arg.c_str());
    EXPECT_STREQ(in.sql.tsum,         T_arg.c_str());
    EXPECT_STREQ(in.sql.sum,          S_arg.c_str());
    EXPECT_STREQ(in.sql.ent,          E_arg.c_str());
    EXPECT_STREQ(in.sql.fin,          F_arg.c_str());
    EXPECT_STREQ(in.insuspect,        W_arg.c_str());
    EXPECT_EQ(in.suspectmethod,       1);
    EXPECT_EQ(in.suspecttime,         1);
    EXPECT_EQ(in.min_level,           (std::size_t) 1);
    EXPECT_EQ(in.max_level,           (std::size_t) 1);
}

TEST(parse_cmd_line, output_arguments) {
    const char opts[] = "o:O:";
    const std::string exec = "exec";
    const std::string o = "-o"; const std::string o_arg = "filename";
    const std::string O = "-O"; const std::string O_arg = "dbname";

    // neither
    {
        const char *argv[] = {
            exec.c_str(),
        };

        int argc = sizeof(argv) / sizeof(argv[0]);

        struct input in;
        ASSERT_EQ(parse_cmd_line(argc, (char **) argv, opts, 0, "", &in), argc);
        EXPECT_EQ(in.output,      STDOUT);
    }

    // -o
    {
        const char *argv[] = {
            exec.c_str(),
            o.c_str(), o_arg.c_str(),
        };

        int argc = sizeof(argv) / sizeof(argv[0]);

        struct input in;
        ASSERT_EQ(parse_cmd_line(argc, (char **) argv, opts, 0, "", &in), argc);
        EXPECT_EQ(in.output,      OUTFILE);
        EXPECT_STREQ(in.outname,  o_arg.c_str());
    }

    // -O
    {
        const char *argv[] = {
            exec.c_str(),
            O.c_str(), O_arg.c_str(),
        };

        int argc = sizeof(argv) / sizeof(argv[0]);

        struct input in;
        ASSERT_EQ(parse_cmd_line(argc, (char **) argv, opts, 0, "", &in), argc);
        EXPECT_EQ(in.output,      OUTDB);
        EXPECT_STREQ(in.outname,  O_arg.c_str());
    }

    // -o then -O
    {
        const char *argv[] = {
            exec.c_str(),
            o.c_str(), o_arg.c_str(),
            O.c_str(), O_arg.c_str(),
        };

        int argc = sizeof(argv) / sizeof(argv[0]);

        struct input in;
        ASSERT_EQ(parse_cmd_line(argc, (char **) argv, opts, 0, "", &in), argc);
        EXPECT_EQ(in.output,      OUTDB);
        EXPECT_STREQ(in.outname,  O_arg.c_str());
    }

    // -O then -o
    {
        const char *argv[] = {
            exec.c_str(),
            O.c_str(), O_arg.c_str(),
            o.c_str(), o_arg.c_str(),
        };

        int argc = sizeof(argv) / sizeof(argv[0]);

        struct input in;
        ASSERT_EQ(parse_cmd_line(argc, (char **) argv, opts, 0, "", &in), argc);
        EXPECT_EQ(in.output,      OUTFILE);
        EXPECT_STREQ(in.outname,  o_arg.c_str());
    }
}

TEST(parse_cmd_line, positional) {
    std::string exec = "exec";
    std::string pos1 = "positional1";
    std::string pos2 = "positional2";

    const char *argv[] = {
        exec.c_str(),
        pos1.c_str(),
        pos2.c_str(),
    };

    int argc = sizeof(argv) / sizeof(argv[0]);

    struct input in;
    // 1, since no options were read
    ASSERT_EQ(parse_cmd_line(argc, (char **) argv, "", 0, "", &in), 1);

    EXPECT_EQ(in.maxthreads,          1);
    EXPECT_EQ(in.delim[0],            '|');
    EXPECT_EQ(in.dontdescend,         0);
    EXPECT_EQ(in.buildinindir,        0);
    EXPECT_EQ(in.suspectd,            0);
    EXPECT_EQ(in.suspectfl,           0);
    EXPECT_EQ(in.suspectfile,         0);
    EXPECT_EQ(in.stride,              0);
    EXPECT_EQ(in.infile,              0);
    EXPECT_EQ(in.min_level,           (size_t) 0);
    EXPECT_EQ(in.max_level,           (size_t) -1);
    EXPECT_STREQ(in.sql.intermediate, "");
    EXPECT_STREQ(in.sql.agg,          "");
}

TEST(INSTALL_STR, good) {
    int retval = 0;
    const char SOURCE[] = "INSTALL_STR good test";
    char dst[MAXPATH];
    INSTALL_STR(dst, SOURCE, MAXPATH, SOURCE);
    EXPECT_EQ(retval, 0);
    EXPECT_STREQ(SOURCE, dst);
}

TEST(INSTALL_STR, bad) {
    int retval = 0;
    const char SOURCE[] = "INSTALL_STR bad test";
    char dst[MAXPATH] = {0};
    INSTALL_STR(dst, SOURCE, 1, SOURCE);
    EXPECT_EQ(retval, -1);
    EXPECT_STRNE(SOURCE, dst);
}

TEST(INSTALL_INT, good) {
    int retval = 0;
    int src = 123;
    int dst = 0;

    char buf[10];
    SNPRINTF(buf, 10, "%d", src);
    INSTALL_INT(dst, buf, -1024, 1024, "INSTALL_INT good test");
    EXPECT_EQ(retval, 0);
    EXPECT_EQ(src, dst);
}

TEST(INSTALL_INT, too_small) {
    int retval = 0;
    int src = -1234;
    int dst = 0;

    char buf[10];
    SNPRINTF(buf, 10, "%d", src);
    INSTALL_INT(dst, buf, -1024, 1024, "INSTALL_INT too small test");
    EXPECT_EQ(retval, -1);
}

TEST(INSTALL_INT, too_big) {
    int retval = 0;
    int src = 1234;
    int dst = 0;

    char buf[10];
    SNPRINTF(buf, 10, "%d", src);
    INSTALL_INT(dst, buf, -1024, 1024, "INSTALL_INT too big test");
    EXPECT_EQ(retval, -1);
}

TEST(INSTALL_UINT, good) {
    int retval = 0;
    std::size_t src = 123;
    std::size_t dst = 0;

    char buf[10];
    SNPRINTF(buf, 10, "%zu", src);
    INSTALL_UINT(dst, buf, (std::size_t) 1, (std::size_t) 1024, "INSTALL_UINT good test");
    EXPECT_EQ(retval, 0);
    EXPECT_EQ(src, dst);
}

TEST(INSTALL_UINT, too_small) {
    int retval = 0;
    std::size_t src = 0;
    std::size_t dst = 0;

    char buf[10];
    SNPRINTF(buf, 10, "%zu", src);
    INSTALL_UINT(dst, buf, (std::size_t) 1, (std::size_t) 1024, "INSTALL_UINT too small test");
    EXPECT_EQ(retval, -1);
    EXPECT_EQ(dst, (std::size_t) 0);
}

TEST(INSTALL_UINT, too_big) {
    int retval = 0;
    std::size_t src = 1234;
    std::size_t dst = 0;

    char buf[10];
    SNPRINTF(buf, 10, "%zu", src);
    INSTALL_UINT(dst, buf, (std::size_t) 1, (std::size_t) 1024, "INSTALL_UINT too big test");
    EXPECT_EQ(retval, -1);
    EXPECT_EQ(src, dst);
}
