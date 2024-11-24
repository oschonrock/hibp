#!/usr/bin/env bash


projdir="$(realpath $(dirname $0)/..)"

if [[ -z $1 ]]
then
    echo "Usage: system_tests.sh BUILDDIR"
    exit 1
fi
builddir="$(realpath $1)"
shift

oneTimeSetUp() {
    export tmpdir=$projdir/test/tmp
    mkdir -p $tmpdir
    rm -rf $tmpdir/*

    stdoutF="${tmpdir}/stdout"
    stderrF="${tmpdir}/stderr"

    $builddir/mock_api_server $projdir/test/data &
    export mock_server_pid=$!
    
    export avoidDoubleTearDownExecution="true"
}

oneTimeTearDown() {
    if [[ "${avoidDoubleTearDownExecution}" == "true" ]]
    then   
	kill $mock_server_pid
	rm -rf $projdir/test/tmp/*
	unset -v avoidDoubleTearDownExecution
    fi
}

th_assertTrueWithNoOutput() {
    th_return_=$1
    th_stdout_=$2
    th_stderr_=$3

    assertTrue 'expecting return code of 0 (true)' ${th_return_}
    assertFalse 'unexpected output to STDOUT' "[ -s '${th_stdout_}' ]"
    assertFalse 'unexpected output to STDERR' "[ -s '${th_stderr_}' ]"

    unset th_return_ th_stdout_ th_stderr_
}

# download

testDownloadSha1() {
    $builddir/hibp-download --testing $projdir/test/tmp/hibp_test.sha1.bin --limit 256 --no-progress >${stdoutF} 2>${stderrF}
    rtrn=$?
    th_assertTrueWithNoOutput ${rtrn} "${stdoutF}" "${stderrF}"
}

testDownloadNtlm() {
    $builddir/hibp-download --testing $projdir/test/tmp/hibp_test.ntlm.bin --ntlm --limit 256 --no-progress >${stdoutF} 2>${stderrF}
    rtrn=$?
    th_assertTrueWithNoOutput ${rtrn} "${stdoutF}" "${stderrF}"
}

# check download

testDownloadCmpSha1() {
    cmp $projdir/test/data/hibp_test.sha1.bin $projdir/test/tmp/hibp_test.sha1.bin >${stdoutF} 2>${stderrF}
    rtrn=$?
    th_assertTrueWithNoOutput ${rtrn} "${stdoutF}" "${stderrF}"
}

testDownloadCmpNtlm() {
    cmp $projdir/test/data/hibp_test.ntlm.bin $projdir/test/tmp/hibp_test.ntlm.bin >${stdoutF} 2>${stderrF}
    rtrn=$?
    th_assertTrueWithNoOutput ${rtrn} "${stdoutF}" "${stderrF}"
}

# make topn

testTopnSha1() {
    :> ${stdoutF} 
    $builddir/hibp-topn test/data/hibp_test.sha1.bin -o test/tmp/hibp_topn.sha1.bin --topn 10000 1>/dev/null  2>${stderrF}
    rtrn=$?
    th_assertTrueWithNoOutput ${rtrn} "${stdoutF}" "${stderrF}"
}

testTopnNtlm() {
    :> ${stdoutF} 
    $builddir/hibp-topn --ntlm test/data/hibp_test.ntlm.bin -o test/tmp/hibp_topn.ntlm.bin --topn 10000 1>/dev/null  2>${stderrF}
    rtrn=$?
    th_assertTrueWithNoOutput ${rtrn} "${stdoutF}" "${stderrF}"
}

# check topn

testTopnCmpSha1() {
    cmp $projdir/test/data/hibp_topn.sha1.bin $projdir/test/tmp/hibp_topn.sha1.bin >${stdoutF} 2>${stderrF}
    rtrn=$?
    th_assertTrueWithNoOutput ${rtrn} "${stdoutF}" "${stderrF}"
}

testTopnCmpNtlm() {
    cmp $projdir/test/data/hibp_topn.ntlm.bin $projdir/test/tmp/hibp_topn.ntlm.bin >${stdoutF} 2>${stderrF}
    rtrn=$?
    th_assertTrueWithNoOutput ${rtrn} "${stdoutF}" "${stderrF}"
}

# search topn

testSearchPlainSha1() {
    plain="truelove15"
    correct_count="1002"
    count=$($builddir/hibp-search $projdir/test/data/hibp_topn.sha1.bin "${plain}" | grep '^found' | cut -d: -f2)
    assertEquals "count for plain pw '${plain}' of '${count}' was wrong" "${correct_count}" "${count}"
}

testSearchPlainNtlm() {
    plain="19696969"
    correct_count="913"
    count=$($builddir/hibp-search --ntlm $projdir/test/data/hibp_topn.ntlm.bin "${plain}" | grep '^found' | cut -d: -f2)
    assertEquals "count for plain pw '${plain}' of '${count}' was wrong" "${correct_count}" "${count}"
}

testSearchHashSha1() {
    hash="00001131628B741FF755AAC0E7C66D26A7C72082"
    correct_count="1002"
    count=$($builddir/hibp-search --hash $projdir/test/data/hibp_topn.sha1.bin "${hash}" | grep '^found' | cut -d: -f2)
    assertEquals "count for hash pw '${hash}' of '${count}' was wrong" "${correct_count}" "${count}"
}

testSearchHashNtlm() {
    hash="0001256EA8F568DBEACE2E172FD939F7"
    correct_count="913"
    count=$($builddir/hibp-search --ntlm --hash $projdir/test/data/hibp_topn.ntlm.bin "${hash}" | grep '^found' | cut -d: -f2)
    assertEquals "count for hash pw '${hash}' of '${count}' was wrong" "${correct_count}" "${count}"
}



. $projdir/ext/shunit2/shunit2


