const childProcess = require('child_process')

/**
 * Helper to run executables. This has been introduced to work around Shell injections[0] while
 * keeping a good developer experience. The template string is split on white spaces and passed to
 * a child_process method that don't use a shell.
 *
 * Prefer this helper over using other child_process methods. Avoid using `child_process.exec`,
 * `child_process.execSync` or the `shell` option of other `child_process` functions.
 *
 * Use this as a tagged string template. Example:
 *
 * > command`git ls-files ${directory}`.run()
 *
 * [0]: https://matklad.github.io/2021/07/30/shell-injection.html
 */
function command(...templateArguments) {
  const [commandName, ...commandArguments] = parseCommandTemplateArguments(...templateArguments)

  let input = ''
  let env
  let currentWorkingDirectory

  return {
    withInput(newInput) {
      input = newInput
      return this
    },

    withEnvironment(newEnv) {
      env = newEnv
      return this
    },

    withCurrentWorkingDirectory(newCurrentWorkingDirectory) {
      currentWorkingDirectory = newCurrentWorkingDirectory
      return this
    },

    run() {
      const commandResult = childProcess.spawnSync(commandName, commandArguments, {
        input,
        env: { ...process.env, ...env },
        cwd: currentWorkingDirectory,
        encoding: 'utf-8',
      })

      if (commandResult.status !== 0) {
        const formattedCommand = `${commandName} ${commandArguments.join(' ')}`
        const formattedStderr = commandResult.stderr ? `\n---- stderr: ----\n${commandResult.stderr}\n----` : ''
        const formattedStdout = commandResult.stdout ? `\n---- stdout: ----\n${commandResult.stdout}\n----` : ''
        const exitCause =
          commandResult.signal !== null
            ? ` due to signal ${commandResult.signal}`
            : commandResult.status !== null
              ? ` with exit status ${commandResult.status}`
              : ''
        const error = new Error(`Command failed${exitCause}: ${formattedCommand}${formattedStderr}${formattedStdout}`)
        error.cause = commandResult.error
        throw error
      }

      if (commandResult.stderr) {
        printError(commandResult.stderr)
      }

      return commandResult.stdout
    },
  }
}

/**
 * Parse template values passed to the `command` template tag, and return a list of arguments to run
 * the command.
 *
 * It expects the same parameters as a template tag: the first parameter is a list of template
 * strings, and the other parameters are template variables. See MDN for tagged template
 * documentation[1].
 *
 * The resulting command arguments is a list of strings generated as if the template literal was
 * split on white spaces. For example:
 *
 * parseCommandTemplateArguments`foo bar` == ['foo', 'bar']
 * parseCommandTemplateArguments`foo ${'bar'} baz` == ['foo', 'bar', 'baz']
 *
 * Template variables are considered as part of the previous or next command argument if they are
 * not separated with a space:
 *
 * parseCommandTemplateArguments`foo${'bar'} baz` == ['foobar', 'baz']
 * parseCommandTemplateArguments`foo ${'bar'}baz` == ['foo', 'barbaz']
 * parseCommandTemplateArguments`foo${'bar'}baz` == ['foobarbaz']
 *
 * Template variables are never split on white spaces, allowing to pass any arbitrary argument to
 * the command without worrying on shell escaping:
 *
 * parseCommandTemplateArguments`foo ${'bar baz'}` == ['foo', 'bar baz']
 *
 * To pass template variables as different command arguments use an array as template argument:
 *
 * parseCommandTemplateArguments`foo ${['bar', 'baz']}` == ['foo', 'bar', 'baz']
 *
 *
 * const commitMessage = 'my commit message'
 * parseCommandTemplateArguments`git commit -c ${commitMessage}` == ['git', 'commit', '-c', 'my commit message']
 *
 * [1]: https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Template_literals#tagged_templates
 */
function parseCommandTemplateArguments(templateStrings, ...templateVariables) {
  let parsedArguments = []
  for (let i = 0; i < templateStrings.length; i += 1) {
    if (i > 0) {
      // Interleave variables with template strings
      if (!parsedArguments.length || templateStrings[i - 1].match(/\s$/)) {
        // If the latest string ends with a space, consider the variable as separate argument(s)
        const variable = templateVariables[i - 1]
        if (Array.isArray(variable)) {
          parsedArguments.push(...variable)
        } else {
          parsedArguments.push(variable)
        }
      } else {
        // Else, append the variable to the latest argument
        parsedArguments[parsedArguments.length - 1] += templateVariables[i - 1]
      }
    }

    const words = templateStrings[i].split(/\s+/).filter((word) => word)

    if (parsedArguments.length && words.length && !templateStrings[i].match(/^\s/)) {
      // If the string does not start with a space, append it to the latest argument
      parsedArguments[parsedArguments.length - 1] += words.shift()
    }

    parsedArguments.push(...words)
  }
  return parsedArguments
}

function runMain(mainFunction) {
  Promise.resolve()
    // The main function can be either synchronous or asynchronous, so let's wrap it in an async
    // callback that will catch both thrown errors and rejected promises
    .then(() => mainFunction())
    .catch((error) => {
      printError('\nScript exited with error:')
      printErrorWithCause(error)
      process.exit(1)
    })
}

const resetColor = '\x1b[0m'

function printError(...params) {
  const redColor = '\x1b[31;1m'
  console.log(redColor, ...params, resetColor)
}

function printErrorWithCause(error) {
  printError(error)
  if (error.cause) {
    printError('Caused by:')
    printErrorWithCause(error.cause)
  }
}

function printLog(...params) {
  const greenColor = '\x1b[32;1m'
  console.log(greenColor, ...params, resetColor)
}

function timeout(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms))
}

module.exports = {
  command,
  printError,
  printLog,
  runMain,
  timeout,
}