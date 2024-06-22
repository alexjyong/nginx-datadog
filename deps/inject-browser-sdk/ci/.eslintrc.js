module.exports = {
  env: {
    browser: false,
    node: true,
    commonjs: true,
    es2021: true,
  },
  extends: ['eslint:recommended', 'prettier'],
  parserOptions: {
    ecmaVersion: 'latest',
  },
  rules: {
    'arrow-body-style': 'error',
    camelcase: ['error', { properties: 'never', allow: ['_dd_temp_'] }],
    curly: 'error',
    eqeqeq: ['error', 'smart'],
    'guard-for-in': 'error',
    'id-denylist': [
      'error',
      'any',
      'Number',
      'number',
      'String',
      'string',
      'Boolean',
      'boolean',
      'Undefined',
      'undefined',
    ],
    'id-match': 'error',
    'no-bitwise': 'error',
    'no-caller': 'error',
    'no-else-return': 'error',
    'no-eq-null': 'error',
    'no-eval': 'error',
    'no-extra-bind': 'error',
    'no-new-func': 'error',
    'no-new-wrappers': 'error',
    'no-return-await': 'error',
    'no-sequences': 'error',
    'no-template-curly-in-string': 'error',
    'no-throw-literal': 'error',
    'no-undef-init': 'error',
    'no-unreachable': 'error',
    'no-useless-concat': 'error',
    'object-shorthand': 'error',
    'one-var': ['error', 'never'],
    'prefer-rest-params': 'off',
    'prefer-template': 'error',
    quotes: ['error', 'single', { avoidEscape: true }],
    radix: 'error',
    'require-await': 'error',
    'spaced-comment': [
      'error',
      'always',
      {
        line: {
          markers: ['/'],
        },
        block: {
          balanced: true,
        },
      },
    ],
    'no-unused-vars': [
      'error',
      {
        args: 'all',
        argsIgnorePattern: '^_',
        vars: 'all',
      },
    ],
  },
}