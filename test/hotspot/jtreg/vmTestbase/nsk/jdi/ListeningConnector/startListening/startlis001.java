/*
 * Copyright (c) 2000, 2018, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

package nsk.jdi.ListeningConnector.startListening;

import com.sun.jdi.Bootstrap;
import com.sun.jdi.connect.*;
import com.sun.jdi.VirtualMachine;

import java.io.*;

import java.net.InetAddress;
import java.net.UnknownHostException;

import java.util.Iterator;
import java.util.List;
import java.util.Map;

import nsk.share.*;
import nsk.share.jpda.*;
import nsk.share.jdi.*;


/**
 * The test exercises JDI function <code>ListeningConnector.startListening</code>.
 * The <b>Socket Listening Connector</b> is using as listening
 * connector.<br>
 * The test cases include:
 * <li> checking that listening address returned by
 * <code>ListeningConnector.startListening()</code> matches the address
 * which was set via connector's arguments;
 * <li> checking that address generated by
 * <code>ListeningConnector.startListening()</code> is valid i.e.
 * debugee VM is accessible via this address.
 */
public class startlis001 {
    static final int PASSED = 0;
    static final int FAILED = 2;
    static final int JCK_STATUS_BASE = 95;
    static final String CONNECTOR_NAME =
        "com.sun.jdi.SocketListen";
    static final String DEBUGEE_CLASS =
        "nsk.jdi.ListeningConnector.startListening.startlis001t";

    private Log log;

    private VirtualMachine vm;
    private ListeningConnector connector;
    private Map<java.lang.String,? extends com.sun.jdi.connect.Connector.Argument> connArgs;
    private PrintStream out;

    IORedirector outRedirector;
    IORedirector errRedirector;

    boolean totalRes = true;

    public static void main (String argv[]) {
        System.exit(run(argv,System.out) + JCK_STATUS_BASE);
    }

    public static int run(String argv[], PrintStream out) {
        return new startlis001().runIt(argv, out);
    }

    private int runIt(String argv[], PrintStream out) {
        String port;
        String addr;
        InetAddress inetAddr = null;
        ArgumentHandler argHandler = new ArgumentHandler(argv);

// pass if CONNECTOR_NAME is not implemented
// on this platform
        if (argHandler.shouldPass(CONNECTOR_NAME))
            return PASSED;
        this.out = out;
        log = new Log(out, argHandler);

        long timeout = argHandler.getWaitTime() * 60 * 1000;

/* Check that listening address returned by ListeningConnector.startListening()
   matches the address which was set via connector's arguments */
        try {
            inetAddr = InetAddress.getLocalHost();
        } catch (UnknownHostException e) {
            log.complain("FAILURE: caught UnknownHostException " +
                e.getMessage());
            totalRes = false;
        }
        String hostname = inetAddr.getHostName();
        String ip = inetAddr.getHostAddress();
        port = argHandler.getTransportPortIfNotDynamic();

        initConnector(port);
        if ((addr = startListen()) == null) {
            log.complain("Test case #1 FAILED: unable to start listening");
            totalRes = false;
        }
        else {
            log.display("Test case #1: start listening the address " + addr);
            log.display("Expected address: "+ hostname + ":" + port +
                "\n\tor "+ ip + ":" + port);
            if ( (!addr.startsWith(hostname) && !addr.startsWith(ip)) ||
                 (port != null && !addr.endsWith(port)) ) {
                log.complain("Test case #1 FAILED: listening address " + addr +
                    "\ndoes not match expected address:\n" +
                    hostname + ":" + port + " or " +
                    ip + ":" + port);
                totalRes = false;
            }
            if (!stopListen()) {
                log.complain("TEST: unable to stop listening #1");
                totalRes = false;
            }
            else
               log.display("Test case #1 PASSED: listening address matches expected address");
        }

/* Check that an address generated by ListeningConnector.startListening()
   is valid i.e. debugee VM is accessible via this address */
        initConnector(null);
        if ((addr = startListen()) == null) {
            log.complain("Test case #2 FAILED: unable to start listening");
            return FAILED;
        }
        else
            log.display("Test case #2: start listening the address " + addr);

        String java = argHandler.getLaunchExecPath()
                          + " " + argHandler.getLaunchOptions();

        String cmd = java +
            " -Xdebug -Xnoagent -Xrunjdwp:transport=dt_socket,server=n,address=" +
            addr + " " + DEBUGEE_CLASS;

        Binder binder = new Binder(argHandler, log);
        Debugee debugee = null;

        log.display("command: " + cmd);
        try {
            debugee = binder.startLocalDebugee(cmd);
            debugee.redirectOutput(log);
        } catch (Exception e) {
            stopListen();
            throw new Failure(e);
        }

        if ((vm = attachTarget()) == null) {
            log.complain("Test case #2 FAILED: unable to attach the debugee VM");
            debugee.close();
            stopListen();
            return FAILED;
        }
        else
            log.display("Test case #2 PASSED: successfully attach the debugee VM");

        if (!stopListen()) {
            log.complain("TEST: unable to stop listening #2");
            debugee.close();
            return FAILED;
        }

        debugee.setupVM(vm);
        debugee.waitForVMInit(timeout);

        log.display("\nResuming debugee VM");
        debugee.resume();

        log.display("\nWaiting for debugee VM exit");
        int code = debugee.waitFor();
        if (code != (JCK_STATUS_BASE+PASSED)) {
            log.complain("Debugee VM has crashed: exit code=" +
                code);
            return FAILED;
        }
        log.display("Debugee VM: exit code=" + code);

        if (totalRes) return PASSED;
        else return FAILED;
    }

    private VirtualMachine attachTarget() {
        try {
            return connector.accept(connArgs);
        } catch (IOException e) {
            log.complain("FAILURE: caught IOException: " +
                e.getMessage());
            e.printStackTrace(out);
            return null;
        } catch (IllegalConnectorArgumentsException e) {
            log.complain("FAILURE: Illegal connector arguments: " +
                e.getMessage());
            e.printStackTrace(out);
            return null;
        } catch (Exception e) {
            log.complain("FAILURE: Exception: " +
                e.getMessage());
            e.printStackTrace(out);
            return null;
        }
    }

    private void initConnector(String port) {
        Connector.Argument arg;

        connector = (ListeningConnector)
            findConnector(CONNECTOR_NAME);

        connArgs = connector.defaultArguments();
        Iterator cArgsValIter = connArgs.keySet().iterator();
        while (cArgsValIter.hasNext()) {
            String argKey = (String) cArgsValIter.next();
            String argVal = null;

            if ((arg = (Connector.Argument) connArgs.get(argKey)) == null) {
                log.complain("Argument " + argKey.toString() +
                    "is not defined for the connector: " +
                    connector.name());
            }
            if (arg.name().equals("port") && port != null)
                arg.setValue(port);

            log.display("\targument name=" + arg.name());
            if ((argVal = arg.value()) != null)
                log.display("\t\tvalue=" + argVal);
            else log.display("\t\tvalue=NULL");
        }
    }

    private String startListen() {
        try {
            return connector.startListening(connArgs);
        } catch (IOException e) {
            log.complain("FAILURE: caught IOException: " +
                e.getMessage());
            e.printStackTrace(out);
            return null;
        } catch (IllegalConnectorArgumentsException e) {
            log.complain("FAILURE: Illegal connector arguments: " +
                e.getMessage());
            e.printStackTrace(out);
            return null;
        } catch (Exception e) {
            log.complain("FAILURE: Exception: " + e.getMessage());
            e.printStackTrace(out);
            return null;
        }
    }

    private boolean stopListen() {
        try {
            connector.stopListening(connArgs);
        } catch (IOException e) {
            log.complain("FAILURE: caught IOException: " +
                e.getMessage());
            e.printStackTrace(out);
            return false;
        } catch (IllegalConnectorArgumentsException e) {
            log.complain("FAILURE: Illegal connector arguments: " +
                e.getMessage());
            e.printStackTrace(out);
            return false;
        } catch (Exception e) {
            log.complain("FAILURE: Exception: " + e.getMessage());
            e.printStackTrace(out);
            return false;
        }

        return true;
    }

    private Connector findConnector(String connectorName) {
        List connectors = Bootstrap.virtualMachineManager().allConnectors();
        Iterator iter = connectors.iterator();

        while (iter.hasNext()) {
            Connector connector = (Connector) iter.next();
            if (connector.name().equals(connectorName)) {
                log.display("Connector name=" + connector.name() +
                    "\n\tdescription=" + connector.description() +
                    "\n\ttransport=" + connector.transport().name());
                return connector;
            }
        }
        throw new Error("No appropriate connector");
    }
}
